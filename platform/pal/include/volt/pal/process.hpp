#pragma once

#include "volt/core/error.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace volt::pal {

/// How a process ended.
enum class ExitReason : std::uint8_t {
  /// Returned from main or called exit.
  kReturned,
  /// Killed by a signal.
  kSignalled,
};

/// What a finished process left behind.
struct ProcessExit {
  ExitReason reason = ExitReason::kReturned;
  /// Exit status, or the signal number when the reason is kSignalled.
  std::int32_t code = 0;
};

/// What to start. SPEC 8.1 runs services as separate processes by default.
struct ProcessConfig {
  /// Path to the executable.
  std::string_view executable;
  /// Arguments after the program name, which the platform supplies itself.
  ///
  /// The span and the views inside it only have to stay alive for the spawn
  /// call; the platform copies whatever it needs to keep.
  std::span<const std::string_view> arguments;
};

/// A child process.
class IProcess {
public:
  IProcess() = default;
  virtual ~IProcess() = default;

  // Deleted because the object owns the child; two owners would both wait.
  IProcess(const IProcess &) = delete;
  IProcess &operator=(const IProcess &) = delete;
  IProcess(IProcess &&) = delete;
  IProcess &operator=(IProcess &&) = delete;

  /// Returns the operating system identifier of the child.
  [[nodiscard]] virtual std::int32_t id() const noexcept = 0;

  /// Reports whether the child has not been reaped yet.
  [[nodiscard]] virtual bool running() const noexcept = 0;

  /// Asks the child to stop, the way a supervisor should before escalating.
  ///
  /// @post   the child was signalled; it may still take time to exit
  /// @errors kResourceUnavailable when the child already exited
  [[nodiscard]] virtual core::expected<void> request_stop() noexcept = 0;

  /// Stops the child without giving it a chance to clean up.
  ///
  /// @post   the child is gone once `wait()` returns
  /// @errors kResourceUnavailable when the child already exited
  [[nodiscard]] virtual core::expected<void> kill() noexcept = 0;

  /// Blocks until the child exits and reports how it ended.
  ///
  /// @post   the child is reaped and `running()` is false
  /// @rt     blocks; supervision only
  /// @errors kResourceUnavailable when the child was already waited for
  [[nodiscard]] virtual core::expected<ProcessExit> wait() noexcept = 0;
};

} // namespace volt::pal
