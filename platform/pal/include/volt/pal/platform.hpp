#pragma once

#include "volt/pal/clock.hpp"
#include "volt/pal/file.hpp"
#include "volt/pal/process.hpp"
#include "volt/pal/shared_memory.hpp"
#include "volt/pal/socket.hpp"
#include "volt/pal/thread.hpp"
#include "volt/pal/timer.hpp"
#include "volt/pal/watchdog_device.hpp"

#include "volt/core/error.hpp"
#include "volt/core/types.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace volt::pal {

/// The one object a program asks for platform facilities.
///
/// Everything the operating system provides enters VOLT through here, so
/// choosing a backend is one decision made once at startup rather than a
/// dependency spread across the codebase. It is also what lets the conformance
/// suite run unchanged against every backend.
///
/// The factories allocate, which is why they belong to startup: SPEC 5.1
/// forbids allocation on the data plane once running.
class IPlatform {
public:
  IPlatform() = default;
  virtual ~IPlatform() = default;

  IPlatform(const IPlatform &) = delete;
  IPlatform &operator=(const IPlatform &) = delete;
  IPlatform(IPlatform &&) = delete;
  IPlatform &operator=(IPlatform &&) = delete;

  /// Returns the clock, which outlives every object this platform hands out.
  [[nodiscard]] virtual IClock &clock() noexcept = 0;

  /// Starts a thread running `entry` under `config`.
  ///
  /// @pre    `config.name` only has to stay alive for the call
  /// @post   on success the thread is running and must be joined
  /// @errors kConfigValueOutOfRange for a priority the policy does not allow,
  ///         kResourceUnavailable when the caller may not set the policy,
  ///         kResourceExhausted when the system refuses another thread
  [[nodiscard]] virtual core::expected<std::unique_ptr<IThread>>
  create_thread(const ThreadConfig &config, ThreadEntry entry) noexcept = 0;

  /// Creates a disarmed timer.
  ///
  /// @errors kResourceExhausted when no timer can be created
  [[nodiscard]] virtual core::expected<std::unique_ptr<ITimer>> create_timer() noexcept = 0;

  /// Creates a shared region and maps it, replacing any region of that name.
  ///
  /// @pre    `bytes` is greater than zero; `name` outlives only the call
  /// @post   the region is zero-filled and removed when the object dies
  /// @errors kConfigValueOutOfRange for a zero size,
  ///         kResourceExhausted when the region cannot be sized or mapped
  [[nodiscard]] virtual core::expected<std::unique_ptr<ISharedMemory>>
  create_shared_memory(std::string_view name, std::size_t bytes) noexcept = 0;

  /// Maps a region another process created.
  ///
  /// @post   the mapping is released, but not removed, when the object dies
  /// @errors kResourceUnavailable when no region carries that name
  [[nodiscard]] virtual core::expected<std::unique_ptr<ISharedMemory>>
  open_shared_memory(std::string_view name) noexcept = 0;

  /// Creates an unbound datagram socket.
  ///
  /// @errors kResourceExhausted when no descriptor is available
  [[nodiscard]] virtual core::expected<std::unique_ptr<ISocket>>
  create_datagram_socket() noexcept = 0;

  /// Opens a file.
  ///
  /// @pre    `path` only has to stay alive for the call
  /// @errors kResourceUnavailable when the path cannot be opened that way
  [[nodiscard]] virtual core::expected<std::unique_ptr<IFile>>
  open_file(std::string_view path, FileMode mode) noexcept = 0;

  /// Starts a child process.
  ///
  /// @post   on success the child is running and must be waited for
  /// @errors kResourceUnavailable when the executable cannot be started,
  ///         kResourceExhausted when the system refuses another process
  [[nodiscard]] virtual core::expected<std::unique_ptr<IProcess>>
  spawn_process(const ProcessConfig &config) noexcept = 0;

  /// Opens the hardware watchdog.
  ///
  /// @post   on most platforms the watchdog starts counting immediately
  /// @errors kResourceUnavailable when the device is absent or not permitted
  [[nodiscard]] virtual core::expected<std::unique_ptr<IWatchdogDevice>>
  open_watchdog(std::string_view path) noexcept = 0;

  /// Pins every current and future page of this process in RAM.
  ///
  /// Without this a page fault on the control path costs milliseconds, which
  /// is several deadlines (SPEC 25).
  ///
  /// @errors kResourceUnavailable when the caller may not lock memory,
  ///         kResourceExhausted when the locked-memory limit is too low
  [[nodiscard]] virtual core::expected<void> lock_memory() noexcept = 0;

  /// Moves the calling thread to a scheduling policy and priority.
  ///
  /// Separate from `create_thread` because the main thread is not created by
  /// this interface and still has to become real-time.
  ///
  /// @errors kConfigValueOutOfRange for a priority the policy does not allow,
  ///         kResourceUnavailable when the caller may not set the policy
  [[nodiscard]] virtual core::expected<void>
  set_current_thread_scheduling(SchedulingPolicy policy, core::Priority priority) noexcept = 0;
};

} // namespace volt::pal
