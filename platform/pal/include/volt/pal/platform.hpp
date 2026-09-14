#pragma once

#include "volt/pal/clock.hpp"
#include "volt/pal/file.hpp"
#include "volt/pal/message_queue.hpp"
#include "volt/pal/process.hpp"
#include "volt/pal/shared_memory.hpp"
#include "volt/pal/socket.hpp"
#include "volt/pal/stream_listener.hpp"
#include "volt/pal/stream_socket.hpp"
#include "volt/pal/thread.hpp"
#include "volt/pal/timer.hpp"
#include "volt/pal/watchdog_device.hpp"

#include "volt/core/error.hpp"
#include "volt/core/types.hpp"

#include <cstddef>
#include <cstdint>
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

  /// Starts listening for stream connections on `local`.
  ///
  /// A zero port asks for an ephemeral one, which the listener then reports.
  ///
  /// @errors kResourceBusy when the endpoint is taken,
  ///         kResourceExhausted when no descriptor is available
  [[nodiscard]] virtual core::expected<std::unique_ptr<IStreamListener>>
  listen_stream(Endpoint local, unsigned backlog) noexcept = 0;

  /// Opens a stream connection to `remote`.
  ///
  /// @post   on success the connection is established and may be written to
  /// @errors kTransientPeerUnreachable when nothing is listening there,
  ///         kResourceExhausted when no descriptor is available
  [[nodiscard]] virtual core::expected<std::unique_ptr<IStreamSocket>>
  connect_stream(Endpoint remote) noexcept = 0;

  /// Starts listening for local stream connections at a filesystem `path`.
  ///
  /// Local streams are the control plane inside one machine (SPEC 10.1):
  /// they carry kernel-attested peer credentials, which TCP cannot.
  ///
  /// A path whose previous owner died without unlinking it is taken over;
  /// a path with a live listener behind it is refused.
  ///
  /// @pre    `path` fits the platform's local-address limit
  /// @errors kConfigValueOutOfRange when the path is too long,
  ///         kResourceBusy when something already listens there,
  ///         kResourceExhausted when no descriptor is available
  [[nodiscard]] virtual core::expected<std::unique_ptr<IStreamListener>>
  listen_local(std::string_view path, unsigned backlog) noexcept = 0;

  /// Opens a local stream connection to the listener at `path`.
  ///
  /// @post   on success the connection is established and carries credentials
  /// @errors kTransientPeerUnreachable when nothing listens there,
  ///         kConfigValueOutOfRange when the path is too long,
  ///         kResourceExhausted when no descriptor is available
  [[nodiscard]] virtual core::expected<std::unique_ptr<IStreamSocket>>
  connect_local(std::string_view path) noexcept = 0;

  /// Creates a kernel message queue, replacing any queue of that name.
  ///
  /// @pre    `config.depth` and `config.message_bytes` are greater than zero
  /// @post   the queue is removed when the object dies
  /// @errors kConfigValueOutOfRange for a zero depth or message size,
  ///         kResourceExhausted when the system refuses the queue
  [[nodiscard]] virtual core::expected<std::unique_ptr<IMessageQueue>>
  create_message_queue(const MessageQueueConfig &config) noexcept = 0;

  /// Opens a message queue another process created.
  ///
  /// @post   the queue outlives this handle; only the creator removes it
  /// @errors kResourceUnavailable when no queue carries that name
  [[nodiscard]] virtual core::expected<std::unique_ptr<IMessageQueue>>
  open_message_queue(std::string_view name) noexcept = 0;

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

  /// Returns the operating system identifier of the calling process.
  ///
  /// Shared-memory structures record it as the owner of a claimed resource,
  /// so that a later opener can ask whether the owner still exists.
  [[nodiscard]] virtual std::int32_t current_process_id() const noexcept = 0;

  /// Reports whether process `identifier` still exists.
  ///
  /// An exited-but-unreaped process still exists here, exactly as the kernel
  /// sees it. The identifier space is reused by every operating system, so a
  /// true result is evidence, not proof; VOLT only relies on the false case,
  /// which is definitive, to reclaim what a dead process left behind.
  ///
  /// @rt     one system call at most; recovery paths only
  [[nodiscard]] virtual bool process_alive(std::int32_t identifier) const noexcept = 0;

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
