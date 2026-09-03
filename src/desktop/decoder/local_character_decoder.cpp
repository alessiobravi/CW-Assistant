#include "local_character_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
#include "onnx_character_decoder_backend.hpp"
#endif

namespace cwassistant::desktop {

namespace {

double stableLaneCenter(
    const cwassistant::core::CwTrackDiagnostic& track) noexcept {
  return std::isfinite(track.presentation_frequency_hz) &&
          track.presentation_frequency_hz > 0.0
      ? track.presentation_frequency_hz
      : track.frequency_hz;
}

}  // namespace

LocalCharacterFrontendBank::Lane::Lane(
    const cwassistant::core::CwCharacterTrackKey key,
    const double center_frequency_hz) {
  frontend.reset(key, center_frequency_hz);
}

LocalCharacterFrontendBank::LocalCharacterFrontendBank(
    const std::size_t maximum_lanes)
    : maximum_lanes_(std::clamp<std::size_t>(maximum_lanes, 1U, 8U)) {}

void LocalCharacterFrontendBank::setEnabled(const bool enabled) noexcept {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  reset();
}

void LocalCharacterFrontendBank::reset() noexcept {
  lanes_.clear();
  input_sequence_ = 0;
  ++generation_;
  if (generation_ == 0U) generation_ = 1U;
  next_lane_generation_ = 1U;
}

std::vector<CwCharacterFeatureWindowPtr> LocalCharacterFrontendBank::process(
    const cwassistant::core::RealtimeSampleBlock& block,
    const std::span<const cwassistant::core::CwTrackDiagnostic> tracks) {
  std::vector<CwCharacterFeatureWindowPtr> result;
  if (!enabled_) return result;
  ++input_sequence_;

  std::vector<const cwassistant::core::CwTrackDiagnostic*> eligible;
  eligible.reserve(tracks.size());
  for (const auto& track : tracks) {
    const bool qualified =
        track.verification_state == cwassistant::core::CwTrackState::Verified ||
        track.verification_state ==
            cwassistant::core::CwTrackState::MorseLikely ||
        track.operator_selected;
    const auto existing = lanes_.find(track.id);
    const std::uint64_t grace_ns = track.operator_selected
        ? 10'000'000'000ULL : 2'000'000'000ULL;
    const bool inside_silence_grace = existing != lanes_.end() &&
        block.timestamp_ns >= existing->second.last_active_timestamp_ns &&
        block.timestamp_ns - existing->second.last_active_timestamp_ns <=
            grace_ns;
    const bool start_manual_lane =
        existing == lanes_.end() && track.operator_selected;
    if (qualified &&
        (track.active || inside_silence_grace || start_manual_lane))
      eligible.push_back(&track);
  }
  std::stable_sort(eligible.begin(), eligible.end(), [](const auto* left,
                                                        const auto* right) {
    const bool left_verified = left->verification_state ==
                               cwassistant::core::CwTrackState::Verified;
    const bool right_verified = right->verification_state ==
                                cwassistant::core::CwTrackState::Verified;
    if (left_verified != right_verified)
      return left_verified > right_verified;
    if (left->operator_selected != right->operator_selected)
      return left->operator_selected > right->operator_selected;
    return left->snr_db > right->snr_db;
  });
  if (eligible.size() > maximum_lanes_) eligible.resize(maximum_lanes_);

  for (const auto* track : eligible) {
    auto lane = lanes_.find(track->id);
    if (lane == lanes_.end()) {
      if (lanes_.size() >= maximum_lanes_) {
        const auto oldest = std::min_element(
            lanes_.begin(), lanes_.end(), [](const auto& left,
                                             const auto& right) {
              return left.second.last_seen_sequence <
                     right.second.last_seen_sequence;
            });
        if (oldest != lanes_.end()) lanes_.erase(oldest);
      }
      const std::uint64_t lane_generation = next_lane_generation_++;
      if (next_lane_generation_ == 0U) next_lane_generation_ = 1U;
      lane = lanes_.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(track->id),
          std::forward_as_tuple(
              cwassistant::core::CwCharacterTrackKey{
                  .track_id = track->id,
                  .track_generation = generation_,
                  .frontend_generation = lane_generation,
              },
              stableLaneCenter(*track))).first;
    }
    lane->second.last_seen_sequence = input_sequence_;
    if (track->active || lane->second.last_active_timestamp_ns == 0U)
      lane->second.last_active_timestamp_ns = block.timestamp_ns;
    // The classical track center is intentionally adaptive and may briefly
    // follow a keyed carrier's FFT sidelobe. Character refinement uses the
    // robust presentation center so that those excursions cannot move its
    // narrow 40 Hz lane away from the actual carrier.
    lane->second.frontend.setCenterFrequency(stableLaneCenter(*track));
    lane->second.frontend.process(block);
    cwassistant::core::CwCharacterFeatureWindow window;
    if (lane->second.frontend.takeWindow(window)) {
      result.push_back(std::make_shared<
                       const cwassistant::core::CwCharacterFeatureWindow>(
          std::move(window)));
    }
  }

  std::erase_if(lanes_, [this](const auto& item) {
    return input_sequence_ > item.second.last_seen_sequence + 500U;
  });
  return result;
}

class LocalCharacterInferenceWorker::Implementation {
 public:
#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
  std::mutex backend_mutex;
  std::shared_ptr<CwOnnxCharacterDecoderBackend> backend;
#endif
};

LocalCharacterInferenceWorker::LocalCharacterInferenceWorker(QObject* parent)
    : QObject(parent), implementation_(std::make_unique<Implementation>()) {}

LocalCharacterInferenceWorker::~LocalCharacterInferenceWorker() = default;

void LocalCharacterInferenceWorker::configure(
    const bool enabled, const QString& model_path,
    const QString& metadata_path) {
  discardPending();
#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
  {
    const std::scoped_lock lock(implementation_->backend_mutex);
    implementation_->backend.reset();
  }
  if (!enabled) {
    emit statusChanged(QStringLiteral("disabled"),
                       QStringLiteral("Local model disabled."));
    return;
  }
  CwCharacterBackendConfig config;
  config.model_path = model_path.toStdString();
  config.metadata_path = metadata_path.toStdString();
  std::shared_ptr<CwOnnxCharacterDecoderBackend> backend;
  try {
    backend =
        std::make_shared<CwOnnxCharacterDecoderBackend>(std::move(config));
  } catch (const std::exception& exception) {
    emit statusChanged(
        QStringLiteral("error"),
        QStringLiteral("Local model setup failed: %1")
            .arg(QString::fromUtf8(exception.what()).left(400)));
    return;
  } catch (...) {
    emit statusChanged(QStringLiteral("error"),
                       QStringLiteral("Local model setup failed."));
    return;
  }
  const auto& diagnostics = backend->diagnostics();
  if (!backend->ready()) {
    emit statusChanged(QStringLiteral("error"),
                       QString::fromStdString(diagnostics.message));
    return;
  }
  {
    const std::scoped_lock lock(implementation_->backend_mutex);
    implementation_->backend = std::move(backend);
  }
  emit statusChanged(QStringLiteral("ready"),
                     QStringLiteral("Local model ready."));
#else
  static_cast<void>(enabled);
  static_cast<void>(model_path);
  static_cast<void>(metadata_path);
  emit statusChanged(
      QStringLiteral("unavailable"),
      QStringLiteral("Local model support is unavailable in this build."));
#endif
}

void LocalCharacterInferenceWorker::submit(
    const int source_mode, CwCharacterFeatureWindowPtr window) {
  if (!window) return;
  bool schedule = false;
  {
    const std::scoped_lock lock(pending_mutex_);
    const auto same_track = std::find_if(
        pending_windows_.begin(), pending_windows_.end(),
        [&](const PendingWindow& pending) {
          return pending.source_mode == source_mode && pending.window &&
                 pending.window->track.track_id == window->track.track_id;
        });
    if (same_track != pending_windows_.end()) {
      same_track->window = std::move(window);
    } else {
      constexpr std::size_t kMaximumPendingWindows = 8U;
      if (pending_windows_.size() >= kMaximumPendingWindows)
        pending_windows_.erase(pending_windows_.begin());
      pending_windows_.push_back(
          {source_mode, queue_generation_, std::move(window)});
    }
    if (!drain_scheduled_) {
      drain_scheduled_ = true;
      schedule = true;
    }
  }
  if (schedule) {
    QMetaObject::invokeMethod(this, [this] { drainOne(); },
                              Qt::QueuedConnection);
  }
}

void LocalCharacterInferenceWorker::discardPending() noexcept {
  {
    const std::scoped_lock lock(pending_mutex_);
    pending_windows_.clear();
    ++queue_generation_;
    if (queue_generation_ == 0U) queue_generation_ = 1U;
  }
#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
  std::shared_ptr<CwOnnxCharacterDecoderBackend> backend;
  {
    const std::scoped_lock lock(implementation_->backend_mutex);
    backend = implementation_->backend;
  }
  if (backend) backend->requestCancellation();
#endif
}

void LocalCharacterInferenceWorker::drainOne() {
  PendingWindow pending;
  {
    const std::scoped_lock lock(pending_mutex_);
    if (pending_windows_.empty()) {
      drain_scheduled_ = false;
      return;
    }
    pending = std::move(pending_windows_.front());
    pending_windows_.erase(pending_windows_.begin());
  }
#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
  std::shared_ptr<CwOnnxCharacterDecoderBackend> backend;
  {
    const std::scoped_lock lock(implementation_->backend_mutex);
    backend = implementation_->backend;
  }
  if (pending.window && backend && backend->ready()) {
    auto hypothesis =
        std::make_shared<cwassistant::core::CwCharacterHypothesis>();
    if (backend->infer(*pending.window, *hypothesis)) {
      bool current = false;
      {
        const std::scoped_lock lock(pending_mutex_);
        current = pending.queue_generation == queue_generation_;
      }
      if (current)
        emit resultReady(pending.source_mode, std::move(hypothesis));
    } else {
      bool current = false;
      {
        const std::scoped_lock lock(pending_mutex_);
        current = pending.queue_generation == queue_generation_;
      }
      if (current) {
        const auto& diagnostics = backend->diagnostics();
        emit statusChanged(QStringLiteral("error"),
                           QString::fromStdString(diagnostics.message));
      }
    }
  }
#endif
  QMetaObject::invokeMethod(this, [this] { drainOne(); },
                            Qt::QueuedConnection);
}

}  // namespace cwassistant::desktop
