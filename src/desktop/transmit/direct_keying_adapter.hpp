#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace cwassistant::desktop {

enum class DirectKeyingLine {
  Rts,
  Dtr,
};

enum class DirectKeyingLineState {
  Unknown,
  Inactive,
  Active,
};

struct DirectKeyingConfig {
  QString port_name;
  DirectKeyingLine ptt_line{DirectKeyingLine::Rts};
  DirectKeyingLine key_line{DirectKeyingLine::Dtr};
  bool ptt_active_high{true};
  bool key_active_high{true};
};

struct DirectKeyingSnapshot {
  bool open_safe{false};
  DirectKeyingLineState ptt{DirectKeyingLineState::Unknown};
  DirectKeyingLineState key{DirectKeyingLineState::Unknown};
  bool fault_or_unknown{true};
  QString fault;
};

// Narrow seam around the two effects that must be deterministic in tests:
// exclusive ownership and serial control-line writes. Implementations must not
// enumerate or probe devices; acquire() receives the one operator-selected port.
class DirectKeyingBackend {
 public:
  using ErrorHandler = std::function<void(QString)>;

  virtual ~DirectKeyingBackend() = default;
  virtual bool acquire(const QString& port_name, QString& error) = 0;
  virtual bool open(QString& error) = 0;
  virtual bool setLine(DirectKeyingLine line, bool high, QString& error) = 0;
  [[nodiscard]] virtual bool isOpen() const noexcept = 0;
  virtual void close() noexcept = 0;
  virtual void releaseOwnership() noexcept = 0;
  virtual void setErrorHandler(ErrorHandler handler) = 0;
};

// Low-level hardware boundary only. It does not schedule Morse, arm TX, choose
// a port, or infer polarity. The first safe slice accepts active-high wiring
// only and always initializes KEY inactive before PTT inactive.
class DirectKeyingAdapter final {
 public:
  DirectKeyingAdapter();
  explicit DirectKeyingAdapter(std::unique_ptr<DirectKeyingBackend> backend);
  ~DirectKeyingAdapter();

  DirectKeyingAdapter(const DirectKeyingAdapter&) = delete;
  DirectKeyingAdapter& operator=(const DirectKeyingAdapter&) = delete;
  DirectKeyingAdapter(DirectKeyingAdapter&&) = delete;
  DirectKeyingAdapter& operator=(DirectKeyingAdapter&&) = delete;

  [[nodiscard]] bool open(const DirectKeyingConfig& config);
  [[nodiscard]] bool setPtt(bool asserted);
  [[nodiscard]] bool setKey(bool asserted);
  // Idempotent and ordered: KEY is released before PTT. Both releases are
  // attempted even if the first one fails.
  [[nodiscard]] bool releaseAll() noexcept;
  void close() noexcept;

  [[nodiscard]] const DirectKeyingSnapshot& snapshot() const noexcept {
    return snapshot_;
  }

 private:
  [[nodiscard]] bool setLogicalLine(DirectKeyingLine line, bool asserted,
                                    DirectKeyingLineState& state);
  void handleBackendError(QString error) noexcept;
  void faultAndClose(QString error, bool release_lines) noexcept;
  void markClosedUnknown() noexcept;
  void refreshUnknownFlag() noexcept;

  std::unique_ptr<DirectKeyingBackend> backend_;
  DirectKeyingConfig config_;
  DirectKeyingSnapshot snapshot_;
  bool ownership_acquired_{false};
  bool handling_fault_{false};
};

}  // namespace cwassistant::desktop
