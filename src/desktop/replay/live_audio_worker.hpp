#pragma once

#include <QAudioFormat>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cwassistant/core/sample_block.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/spsc_ring_buffer.hpp"
#include "../visualization/spectrum_frame.hpp"

class QAudioSource;
class QIODevice;

namespace cwassistant::desktop {

struct LiveAudioPipe {
  cwassistant::core::SpscRingBuffer<cwassistant::core::RealtimeSampleBlock, 32>
      blocks;
  std::atomic<qulonglong> overruns{0};
};

class LiveAudioCaptureWorker final : public QObject {
  Q_OBJECT

 public:
  explicit LiveAudioCaptureWorker(std::shared_ptr<LiveAudioPipe> pipe,
                                  QObject* parent = nullptr);
  ~LiveAudioCaptureWorker() override;

 public slots:
  void start(const QString& encoded_device_id);
  void stop();

 signals:
  void started(const QString& device_name, double sample_rate,
               int channel_count);
  void stopped();
  void failed(const QString& message);
  void overrunCountChanged(qulonglong count);

 private slots:
  void consumeAvailableBytes();
  void handleStateChanged();

 private:
  [[nodiscard]] float readSample(const char* data) const noexcept;
  void appendFrame(const char* frame);
  void publishBlock();

  static constexpr std::size_t kPublishedBlockSamples = 2'048;
  static constexpr qsizetype kRawBufferBytes = 65'536;

  std::shared_ptr<LiveAudioPipe> pipe_;
  QAudioSource* source_{nullptr};
  QIODevice* input_{nullptr};
  QAudioFormat format_;
  std::array<char, static_cast<std::size_t>(kRawBufferBytes)> raw_buffer_{};
  qsizetype pending_bytes_{0};
  cwassistant::core::RealtimeSampleBlock block_{};
  std::uint64_t sequence_{0};
  std::uint64_t captured_samples_{0};
  bool stopping_{false};
};

class LiveAudioDspWorker final : public QObject {
  Q_OBJECT

 public:
  explicit LiveAudioDspWorker(std::shared_ptr<LiveAudioPipe> pipe,
                              QObject* parent = nullptr);

 public slots:
  void start();
  void stop();
  void configure(int averaging_frames, int frame_rate_hz, bool dc_rejection,
                 bool automatic_gain, double gain_db,
                 double automatic_gain_target_dbfs,
                 bool automatic_bandwidth, double lower_frequency_hz,
                 double upper_frequency_hz);

signals:
  void frameProduced(const cwassistant::desktop::SpectrumFrame& frame);
  void decoderProduced(const QVariantList& channels);

 private slots:
  void drain();

 private:
  std::shared_ptr<LiveAudioPipe> pipe_;
  QTimer timer_;
  cwassistant::core::SpectrumAnalyzer analyzer_;
  cwassistant::core::CwChannelBank decoder_;
};

}  // namespace cwassistant::desktop
