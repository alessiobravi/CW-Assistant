#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cwassistant/core/cw_character_decoder.hpp"
#include "cwassistant/core/cw_character_lane_frontend.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"

namespace cwassistant::desktop {

using CwCharacterFeatureWindowPtr =
    std::shared_ptr<const cwassistant::core::CwCharacterFeatureWindow>;
using CwCharacterHypothesisPtr =
    std::shared_ptr<const cwassistant::core::CwCharacterHypothesis>;

// Maintains only a small number of expensive character-refinement lanes.
// It observes operator-selected or already-verified tracks and never changes
// the classical channel bank's detection/verification decisions.
class LocalCharacterFrontendBank final {
 public:
  explicit LocalCharacterFrontendBank(std::size_t maximum_lanes = 4U);

  void setEnabled(bool enabled) noexcept;
  void reset() noexcept;
  [[nodiscard]] std::vector<CwCharacterFeatureWindowPtr> process(
      const cwassistant::core::RealtimeSampleBlock& block,
      std::span<const cwassistant::core::CwTrackDiagnostic> tracks);

 private:
  struct Lane {
    explicit Lane(cwassistant::core::CwCharacterTrackKey key,
                  double center_frequency_hz);
    cwassistant::core::CwCharacterLaneFrontend frontend;
    std::uint64_t last_seen_sequence{0};
    std::uint64_t last_active_timestamp_ns{0};
  };

  std::unordered_map<std::uint64_t, Lane> lanes_;
  std::size_t maximum_lanes_{4U};
  std::uint64_t input_sequence_{0};
  std::uint64_t generation_{1};
  std::uint64_t next_lane_generation_{1};
  bool enabled_{false};
};

class LocalCharacterInferenceWorker final : public QObject {
  Q_OBJECT

 public:
  explicit LocalCharacterInferenceWorker(QObject* parent = nullptr);
  ~LocalCharacterInferenceWorker() override;

 public slots:
  void configure(bool enabled, const QString& model_path,
                 const QString& metadata_path);

 public:
  // Thread-safe latest-window submission. Callers use a direct connection so
  // Qt's event queue cannot grow without bound when inference is slower than
  // the feature producers.
  void submit(int source_mode, CwCharacterFeatureWindowPtr window);
  void discardPending() noexcept;

 signals:
  void resultReady(int source_mode, CwCharacterHypothesisPtr hypothesis);
  void statusChanged(const QString& state, const QString& detail);

 private:
  void drainOne();
  struct PendingWindow {
    int source_mode{0};
    std::uint64_t queue_generation{0};
    CwCharacterFeatureWindowPtr window;
  };
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
  std::mutex pending_mutex_;
  std::vector<PendingWindow> pending_windows_;
  bool drain_scheduled_{false};
  std::uint64_t queue_generation_{1};
};

}  // namespace cwassistant::desktop

Q_DECLARE_METATYPE(cwassistant::desktop::CwCharacterFeatureWindowPtr)
Q_DECLARE_METATYPE(cwassistant::desktop::CwCharacterHypothesisPtr)
