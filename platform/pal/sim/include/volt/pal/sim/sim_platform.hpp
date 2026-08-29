#pragma once

#include "sim_config.hpp"

#include "volt/pal/platform.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace volt::pal::sim {

namespace detail {
class SimWorld;
} // namespace detail

/// The deterministic implementation of the platform.
///
/// Everything the operating system would decide is decided here instead, from
/// a seed the caller supplies: when a datagram arrives, which of them is lost,
/// what the clock reads, in what order threads run. Two runs of the same
/// scenario under the same seed therefore produce the same events, which is
/// what turns a rare concurrency bug from something seen once into something
/// that can be replayed (SPEC 21.1).
///
/// Nothing here consults the host: no real clock, no real network, no entropy.
/// Objects created by this platform hold a reference into it, so the platform
/// must outlive them.
class SimPlatform final : public IPlatform {
public:
  /// Builds a world from `config`.
  explicit SimPlatform(const SimConfig &config);

  /// Declared here and defined where SimWorld is complete, so the header does
  /// not have to expose the world's contents.
  ~SimPlatform() override;

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

  /// Always succeeds: a simulated process has no pages to pin.
  [[nodiscard]] core::expected<void> lock_memory() noexcept override;

  /// Validates the request the way a real platform would, then records it.
  ///
  /// A simulation has one thread and no scheduler to configure, but a caller
  /// that passes an impossible priority has a bug that must surface here
  /// rather than only on the target.
  [[nodiscard]] core::expected<void>
  set_current_thread_scheduling(SchedulingPolicy policy, core::Priority priority) noexcept override;

  /// Declares that a program exists at `path` and how it ends.
  ///
  /// A simulated world has no filesystem of executables, so a scenario states
  /// which programs it expects to be able to start.
  void register_program(std::string_view path, ProcessExit outcome);

  /// Declares where the simulated watchdog device answers.
  void set_watchdog_path(std::string_view path);

  /// Returns the digest of every event this world has produced.
  ///
  /// Two runs of one scenario under one seed must agree on this value. They
  /// disagree the moment something in the world stops being deterministic,
  /// which is the property the whole backend exists to provide.
  [[nodiscard]] std::uint64_t event_digest() const noexcept;

private:
  std::unique_ptr<detail::SimWorld> world_;
  std::unique_ptr<IClock> clock_;
};

} // namespace volt::pal::sim
