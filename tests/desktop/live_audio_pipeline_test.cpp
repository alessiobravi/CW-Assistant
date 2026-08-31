#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <memory>
#include <numbers>

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
        const bool valid = frame.bins_dbfs.size() == 1'025 &&
                           frame.lower_frequency_hz == 0.0 &&
                           frame.upper_frequency_hz == 24'000.0;
        application.exit(valid ? 0 : 1);
      });

  cwassistant::core::RealtimeSampleBlock block;
  block.stream.sample_rate_hz = 48'000.0;
  block.sample_count = 2'048;
  for (std::size_t index = 0; index < block.sample_count; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> * 1'000.0F *
                        static_cast<float>(index) / 48'000.0F;
    block.samples[index] = {0.5F * std::sin(phase), 0.0F};
  }
  dsp_thread.start();
  QMetaObject::invokeMethod(
      worker, "configure", Qt::BlockingQueuedConnection, Q_ARG(int, 1),
      Q_ARG(bool, true), Q_ARG(bool, false), Q_ARG(double, 0.0),
      Q_ARG(double, -12.0), Q_ARG(bool, false), Q_ARG(double, 0.0),
      Q_ARG(double, 24'000.0));
  QMetaObject::invokeMethod(worker, "start", Qt::BlockingQueuedConnection);
  if (!pipe->blocks.try_push(block)) {
    QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
    dsp_thread.quit();
    dsp_thread.wait();
    return 2;
  }

  QTimer::singleShot(2'000, &application, [&application] {
    application.exit(3);
  });
  const int result = application.exec();
  QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
  dsp_thread.quit();
  dsp_thread.wait();
  return result;
}
