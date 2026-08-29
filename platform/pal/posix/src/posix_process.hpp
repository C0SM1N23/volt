#pragma once

#include "volt/pal/process.hpp"

#include <cstdint>
#include <sys/types.h>

namespace volt::pal::posix {

/// A child process started with `posix_spawn`.
///
/// `posix_spawn` rather than a hand-written fork/exec pair: between fork and
/// exec only async-signal-safe calls are allowed, and VOLT spawns services
/// from a process that already has threads, where allocating or locking in
/// that window can deadlock the child. The library performs the same fork and
/// exec, in code written to respect that rule.
class PosixProcess final : public IProcess {
public:
  /// Adopts a started child. Only the platform calls this.
  explicit PosixProcess(::pid_t identifier) noexcept : identifier_{identifier} {}

  /// Reaps the child if the owner never waited, so no zombie is left behind.
  ~PosixProcess() override;

  [[nodiscard]] std::int32_t id() const noexcept override;
  [[nodiscard]] bool running() const noexcept override;
  [[nodiscard]] core::expected<void> request_stop() noexcept override;
  [[nodiscard]] core::expected<void> kill() noexcept override;
  [[nodiscard]] core::expected<ProcessExit> wait() noexcept override;

private:
  [[nodiscard]] core::expected<void> signal(int signal_number) const noexcept;

  ::pid_t identifier_;
  bool reaped_ = false;
};

} // namespace volt::pal::posix
