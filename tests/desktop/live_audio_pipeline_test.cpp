#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string_view>
#include <vector>

#include "replay/live_audio_worker.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  auto pipe = std::make_shared<cwassistant::desktop::LiveAudioPipe>();
  QThread dsp_thread;
  auto* worker = new cwassistant::desktop::LiveAudioDspWorker(pipe);
  worker->moveToThread(&dsp_thread);
  QObject::connect(&dsp_thread, &QThread::finished, worker,
                   &QObject::deleteLater);

  QObject::connect(
      worker, &cwassistant::desktop::LiveAudioDspWorker::frameProduced,
      &application,
      [&application](const cwassistant::desktop::SpectrumFrame& frame) {
        application.setProperty(
            "validFrame", frame.bins_dbfs.size() == 1'025 &&
                              frame.instantaneous_bins_dbfs.size() == 1'025 &&
                              frame.lower_frequency_hz == 0.0 &&
                              frame.upper_frequency_hz == 24'000.0);
      });
  QString capture_base_path;
  QVariantList last_channels;
  QVariantMap last_diagnostics;
  QObject::connect(
      worker, &cwassistant::desktop::LiveAudioDspWorker::debugCaptureStateChanged,
      &application,
      [&capture_base_path](const bool, const QString& path, const double,
                           const QString&) { capture_base_path = path; });

  QObject::connect(
      worker, &cwassistant::desktop::LiveAudioDspWorker::decoderProduced,
      &application, [&application, worker, &last_channels](
                        const QVariantList& channels) {
        last_channels = channels;
        const auto channel = channels.isEmpty()
            ? QVariantMap{}
            : channels.front().toMap();
        const auto character_evidence =
            channel.value(QStringLiteral("characterEvidence")).toList();
        const bool valid_decoder = channels.size() == 1 &&
            channel.value(QStringLiteral("verifiedCw")).toBool() &&
            channel.value(QStringLiteral("verificationState")).toString() ==
                QStringLiteral("verified") &&
            channel.value(QStringLiteral("verificationReason")).toString() ==
                QStringLiteral("verified") &&
            channel.value(QStringLiteral("verificationConfidence")).toDouble() >
                0.5 &&
            !character_evidence.isEmpty() &&
            character_evidence.front().toMap()
                .value(QStringLiteral("known")).toBool() &&
            std::abs(channel.value(QStringLiteral("frequencyHz")).toDouble() -
                     1'000.0) < 30.0 &&
            channel.value(QStringLiteral("snrDb")).toDouble() > 6.0;
        if (application.property("validFrame").toBool() && valid_decoder) {
          QMetaObject::invokeMethod(worker, "stopDebugCapture",
                                    Qt::BlockingQueuedConnection);
          application.exit(0);
        }
      });
  QObject::connect(
      worker, &cwassistant::desktop::LiveAudioDspWorker::diagnosticsProduced,
      &application, [&last_diagnostics](const QVariantMap& diagnostics) {
        last_diagnostics = diagnostics;
      });

  constexpr double sample_rate_hz = 48'000.0;
  constexpr std::size_t block_samples = 2'048;
  constexpr std::size_t dit_samples = 2'880;  // 20 WPM, 60 ms.
  std::vector<bool> key_units;
  const auto append_units = [&key_units](const bool keyed,
                                          const std::size_t units) {
    key_units.insert(key_units.end(), units, keyed);
  };
  const auto append_letter = [&append_units](const std::string_view elements) {
    for (std::size_t element = 0; element < elements.size(); ++element) {
      append_units(true, elements[element] == '.' ? 1U : 3U);
      append_units(false, element + 1 == elements.size() ? 3U : 1U);
    }
  };
  // Five repetitions leave a deterministic post-symbol evidence interval for
  // the verification entry hysteresis while exercising continued hypotheses.
  for (int repetition = 0; repetition < 5; ++repetition) {
    append_letter("...");
    append_letter("---");
    append_letter("...");
    append_units(false, 4);  // Extend the last character gap to a word gap.
  }

  const std::size_t total_samples = key_units.size() * dit_samples;
  const std::size_t block_count =
      (total_samples + block_samples - 1) / block_samples;
  std::vector<cwassistant::core::RealtimeSampleBlock> blocks(block_count);
  double phase = 0.0;
  for (std::size_t block_index = 0; block_index < blocks.size();
       ++block_index) {
    auto& block = blocks[block_index];
    block.stream.sample_rate_hz = sample_rate_hz;
    block.sequence = block_index;
    block.timestamp_ns = static_cast<std::uint64_t>(
        static_cast<long double>(block_index * block_samples) *
        1'000'000'000.0L / sample_rate_hz);
    block.sample_count = std::min(
        block_samples, total_samples - block_index * block_samples);
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      const std::size_t absolute_sample = block_index * block_samples + index;
      const bool keyed = key_units[absolute_sample / dit_samples];
      block.samples[index] = {
          keyed ? 0.5F * static_cast<float>(std::sin(phase)) : 0.0F, 0.0F};
      phase += 2.0 * std::numbers::pi * 1'000.0 / sample_rate_hz;
    }
  }
  dsp_thread.start();
  QMetaObject::invokeMethod(
      worker, "configure", Qt::BlockingQueuedConnection, Q_ARG(int, 1),
      Q_ARG(int, 60),
      Q_ARG(bool, true), Q_ARG(bool, false), Q_ARG(double, 0.0),
      Q_ARG(double, -12.0), Q_ARG(bool, false), Q_ARG(double, 0.0),
      Q_ARG(double, 24'000.0));
  QMetaObject::invokeMethod(worker, "start", Qt::BlockingQueuedConnection);
  QMetaObject::invokeMethod(
      worker, "setRadioFrequencyContext", Qt::BlockingQueuedConnection,
      Q_ARG(bool, true), Q_ARG(qulonglong, 7'016'450ULL),
      Q_ARG(qulonglong, 0ULL), Q_ARG(bool, false));

  QTemporaryDir capture_root;
  QMetaObject::invokeMethod(worker, "startDebugCapture",
                            Qt::BlockingQueuedConnection,
                            Q_ARG(QString, capture_root.path()));

  std::size_t next_block = 0;
  QTimer feeder;
  feeder.setInterval(1);
  QObject::connect(&feeder, &QTimer::timeout, &application,
                   [&blocks, &next_block, &pipe, &feeder] {
    if (next_block < blocks.size() &&
        pipe->blocks.try_push(blocks[next_block])) {
      ++next_block;
    }
    if (next_block == blocks.size()) feeder.stop();
  });
  feeder.start();

  QTimer::singleShot(10'000, &application,
                     [&application, &last_channels, &last_diagnostics] {
    qCritical().noquote()
        << "live pipeline timeout: validFrame="
        << application.property("validFrame").toBool()
        << "channels=" << last_channels
        << "diagnostics=" << last_diagnostics;
    application.exit(3);
  });
  const int result = application.exec();
  QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
  dsp_thread.quit();
  dsp_thread.wait();
  if (result != 0) {
    return result;
  }

  if (capture_base_path.isEmpty()) {
    return 4;
  }
  QFile wav_file(capture_base_path + QStringLiteral("/audio.wav"));
  QFile log_file(capture_base_path + QStringLiteral("/diagnostics.jsonl"));
  if (!wav_file.exists() || wav_file.size() <= 44) {
    return 5;  // Missing or header-only capture audio.
  }
  if (!log_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return 6;
  }
  const QByteArray first_line = log_file.readLine();
  if (!first_line.contains("\"tracks\"") ||
      !first_line.contains("\"candidateTracks\"")) {
    return 7;  // Diagnostics log line missing expected structure.
  }
  if (!first_line.contains("\"radio\"") ||
      !first_line.contains("7016450") ||
      !first_line.contains("\"available\":true")) {
    return 8;  // Radio frequency context missing from the diagnostics log.
  }
  return 0;
}
