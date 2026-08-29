#pragma once

#include "volt/pal/platform.hpp"

#include "posix_clock.hpp"

namespace volt::pal::posix {

/// The Linux implementation of the platform.
///
/// This is the only place in VOLT that calls the operating system: everything
/// above it goes through `IPlatform`, which is what makes the QNX and
/// simulation backends drop-in replacements rather than rewrites.
class PosixPlatform final : public IPlatform {
public:
  PosixPlatform() = default;

  [[nodiscard]] IClock &clock() noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IThread>>
  create_thread(const ThreadConfig &config, ThreadEntry entry) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<ITimer>> create_timer() noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<ISharedMemory>>
  create_shared_memory(std::string_view name, std::size_t bytes) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<ISharedMemory>>
  open_shared_memory(std::string_view name) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<ISocket>> create_datagram_socket() noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IStreamListener>>
  listen_stream(Endpoint local, unsigned backlog) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IStreamSocket>>
  connect_stream(Endpoint remote) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IFile>> open_file(std::string_view path,
                                                                 FileMode mode) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IProcess>>
  spawn_process(const ProcessConfig &config) noexcept override;

  [[nodiscard]] core::expected<std::unique_ptr<IWatchdogDevice>>
  open_watchdog(std::string_view path) noexcept override;

  [[nodiscard]] core::expected<void> lock_memory() noexcept override;

  [[nodiscard]] core::expected<void>
  set_current_thread_scheduling(SchedulingPolicy policy, core::Priority priority) noexcept override;

private:
  PosixClock clock_;
};

} // namespace volt::pal::posix
