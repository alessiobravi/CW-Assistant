#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>

#include "../visualization/spectrum_frame.hpp"

namespace cwassistant::desktop {

class ReplayController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(bool sourceLoaded READ sourceLoaded NOTIFY stateChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
  Q_PROPERTY(double sampleRate READ sampleRate NOTIFY stateChanged)
  Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY stateChanged)
  Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY stateChanged)
  Q_PROPERTY(int averagingFrames READ averagingFrames WRITE setAveragingFrames
                 NOTIFY averagingFramesChanged)

 public:
  explicit ReplayController(QObject* parent = nullptr);
  ~ReplayController() override;

  [[nodiscard]] const QString& sourceName() const noexcept;
  [[nodiscard]] const QString& statusText() const noexcept;
  [[nodiscard]] bool sourceLoaded() const noexcept;
  [[nodiscard]] bool playing() const noexcept;
  [[nodiscard]] double sampleRate() const noexcept;
  [[nodiscard]] double durationSeconds() const noexcept;
  [[nodiscard]] double positionSeconds() const noexcept;
  [[nodiscard]] int averagingFrames() const noexcept;
  void setAveragingFrames(int value);

  Q_INVOKABLE void openFile(const QUrl& url);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();

 signals:
  void stateChanged();
  void sourceReset();
  void frameReady(const cwassistant::desktop::SpectrumFrame& frame);
  void averagingFramesChanged();

  void openRequested(const QString& path);
  void playRequested();
  void pauseRequested();
  void stopRequested();
  void configureRequested(int averaging_frames);

 private:
  void setStatus(QString status);

  QThread worker_thread_;
  QObject* worker_{nullptr};
  QString source_name_;
  QString status_text_{QStringLiteral("Choose a WAV recording to begin")};
  bool source_loaded_{false};
  bool playing_{false};
  double sample_rate_{0.0};
  double duration_seconds_{0.0};
  double position_seconds_{0.0};
  int averaging_frames_{3};
};

}  // namespace cwassistant::desktop
