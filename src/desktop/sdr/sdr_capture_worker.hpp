#pragma once

#include "replay/live_audio_pipe.hpp"
#include "sdr_receiver.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

namespace cwassistant::desktop {

// Runs only on its owning capture thread. It never exposes transmit operations.
class SdrCaptureWorker final : public QObject {
  Q_OBJECT

 public:
  explicit SdrCaptureWorker(std::shared_ptr<LiveAudioPipe> pipe,
                            QObject* parent = nullptr);
  SdrCaptureWorker(std::shared_ptr<LiveAudioPipe> pipe,
                   std::unique_ptr<SdrReceiveBackend> backend,
                   QObject* parent = nullptr);
  ~SdrCaptureWorker() override;

 public slots:
  void start(const QString& device_id, double center_frequency_hz,
             double sample_rate_hz, double bandwidth_hz,
             const QString& antenna, bool automatic_gain, double gain_db);
  void stop();
  void requestRetune(double center_frequency_hz);

 signals:
  void started(const QString& device_id, double actual_center_frequency_hz,
               double actual_sample_rate_hz, double actual_bandwidth_hz,
               bool actual_automatic_gain, double actual_gain_db);
  void stopped();
  void retuned(double actual_center_frequency_hz);
  void retuneFailed(const QString& message);
  void failed(const QString& message);
  void diagnosticsChanged(qulonglong source_overruns,
                          qulonglong device_overflows,
                          qulonglong read_timeouts,
                          qulonglong read_errors);

 private slots:
  void pump();
  void applyPendingRetune();

 private:
  std::shared_ptr<LiveAudioPipe> pipe_;
  SdrReceiver receiver_;
  QTimer pump_timer_;
  QTimer retune_timer_;
  std::optional<double> pending_retune_hz_;
  bool running_{false};
  std::uint64_t reported_source_overruns_{0};
  std::uint64_t reported_device_overflows_{0};
  std::uint64_t reported_read_timeouts_{0};
  std::uint64_t reported_read_errors_{0};
};

}  // namespace cwassistant::desktop
