#include <QCoreApplication>
#include <QTimer>

#include <cmath>
#include <memory>
#include <numbers>

#include "replay/live_audio_worker.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  auto pipe = std::make_shared<cwassistant::desktop::LiveAudioPipe>();
  cwassistant::desktop::LiveAudioDspWorker worker(pipe);

  QObject::connect(
      &worker, &cwassistant::desktop::LiveAudioDspWorker::frameProduced,
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
  worker.start(1);
  if (!pipe->blocks.try_push(block)) {
    return 2;
  }

  QTimer::singleShot(2'000, &application, [&application] {
    application.exit(3);
  });
  return application.exec();
}
