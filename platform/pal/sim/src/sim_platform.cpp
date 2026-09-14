#include "volt/pal/sim/sim_platform.hpp"

#include "sim_clock.hpp"
#include "sim_file.hpp"
#include "sim_message_queue.hpp"
#include "sim_process.hpp"
#include "sim_shared_memory.hpp"
#include "sim_socket.hpp"
#include "sim_stream_listener.hpp"
#include "sim_stream_socket.hpp"
#include "sim_thread.hpp"
#include "sim_timer.hpp"
#include "sim_watchdog_device.hpp"
#include "sim_world.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace volt::pal::sim {
namespace {

// The real-time priority range VOLT targets, from the POSIX values SPEC 42.2
// assigns to its threads. Validated here so a configuration mistake fails the
// same way in simulation as it does on the target.
constexpr int kLowestRealTimePriority = 1;
constexpr int kHighestRealTimePriority = 99;

[[nodiscard]] core::expected<void> validate_scheduling(SchedulingPolicy policy,
                                                       core::Priority priority) noexcept {
  if (policy == SchedulingPolicy::kDeadline) {
    // A reservation cannot be expressed as a priority; the deadline call is
    // the way in, exactly as on the POSIX backend.
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  const auto requested = static_cast<int>(priority.value());
  if (policy == SchedulingPolicy::kOther) {
    // The time-sharing policy has one priority; asking for another means the
    // caller believed it was requesting real-time behaviour.
    if (requested != 0) {
      return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
    }
    return {};
  }
  if (requested < kLowestRealTimePriority || requested > kHighestRealTimePriority) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

} // namespace

SimPlatform::SimPlatform(const SimConfig &config)
    : world_{std::make_unique<detail::SimWorld>(config)},
      clock_{std::make_unique<SimClock>(*world_)} {}

SimPlatform::~SimPlatform() = default;

IClock &SimPlatform::clock() noexcept { return *clock_; }

core::expected<std::unique_ptr<IThread>> SimPlatform::create_thread(const ThreadConfig &config,
                                                                    ThreadEntry entry) noexcept {
  const core::expected<void> valid = validate_scheduling(config.policy, config.priority);
  if (!valid.has_value()) {
    return std::unexpected{valid.error()};
  }

  std::string name{config.name.substr(0, std::min(config.name.size(), kMaxThreadNameLength))};
  const detail::SimScheduler::Handle handle = world_->scheduler().add(std::move(entry));
  world_->record("thread.create", handle);
  return std::make_unique<SimThread>(*world_, handle, std::move(name));
}

core::expected<std::unique_ptr<ITimer>> SimPlatform::create_timer() noexcept {
  return std::make_unique<SimTimer>(*world_);
}

core::expected<std::unique_ptr<ISharedMemory>>
SimPlatform::create_shared_memory(std::string_view name, std::size_t bytes) noexcept {
  if (bytes == 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  const std::span<std::byte> region = world_->create_region(name, bytes);
  return std::make_unique<SimSharedMemory>(std::string{name}, region);
}

core::expected<std::unique_ptr<ISharedMemory>>
SimPlatform::open_shared_memory(std::string_view name) noexcept {
  const std::optional<std::span<std::byte>> region = world_->find_region(name);
  if (!region.has_value()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return std::make_unique<SimSharedMemory>(std::string{name}, *region);
}

core::expected<std::unique_ptr<ISocket>> SimPlatform::create_datagram_socket() noexcept {
  return std::make_unique<SimSocket>(*world_, world_->network().open());
}

core::expected<std::unique_ptr<IStreamListener>>
SimPlatform::listen_stream(Endpoint local, unsigned backlog) noexcept {
  const core::expected<detail::SimNetwork::SocketId> listener =
      world_->network().listen(local, backlog);
  if (!listener.has_value()) {
    return std::unexpected{listener.error()};
  }
  world_->record("listener.open", *listener);
  return std::make_unique<SimStreamListener>(*world_, *listener);
}

core::expected<std::unique_ptr<IStreamSocket>>
SimPlatform::connect_stream(Endpoint remote) noexcept {
  const core::expected<detail::SimNetwork::ConnectionId> connection =
      world_->network().connect(remote);
  if (!connection.has_value()) {
    return std::unexpected{connection.error()};
  }
  world_->record("stream.connect", *connection);
  return std::make_unique<SimStreamSocket>(*world_, *connection, detail::StreamSide::kClient);
}

core::expected<std::unique_ptr<IStreamListener>>
SimPlatform::listen_local(std::string_view path, unsigned backlog) noexcept {
  const core::expected<detail::SimNetwork::SocketId> listener =
      world_->network().listen_local(path, backlog);
  if (!listener.has_value()) {
    return std::unexpected{listener.error()};
  }
  world_->record("listener.local", *listener);
  return std::make_unique<SimStreamListener>(*world_, *listener);
}

core::expected<std::unique_ptr<IStreamSocket>>
SimPlatform::connect_local(std::string_view path) noexcept {
  const core::expected<detail::SimNetwork::ConnectionId> connection =
      world_->network().connect_local(path);
  if (!connection.has_value()) {
    return std::unexpected{connection.error()};
  }
  world_->record("stream.connect_local", *connection);
  return std::make_unique<SimStreamSocket>(*world_, *connection, detail::StreamSide::kClient, true);
}

core::expected<std::unique_ptr<IMessageQueue>>
SimPlatform::create_message_queue(const MessageQueueConfig &config) noexcept {
  if (config.depth == 0 || config.message_bytes == 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  static_cast<void>(world_->create_message_queue(config.name, config.depth, config.message_bytes));
  world_->record("mq.create", config.depth);
  return std::make_unique<SimMessageQueue>(*world_, std::string{config.name}, true);
}

core::expected<std::unique_ptr<IMessageQueue>>
SimPlatform::open_message_queue(std::string_view name) noexcept {
  if (world_->find_message_queue(name) == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return std::make_unique<SimMessageQueue>(*world_, std::string{name}, false);
}

core::expected<std::unique_ptr<IFile>> SimPlatform::open_file(std::string_view path,
                                                              FileMode mode) noexcept {
  if (mode == FileMode::kRead) {
    std::vector<std::byte> *const content = world_->find_file(path);
    if (content == nullptr) {
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }
    return std::make_unique<SimFile>(*world_, *content, mode);
  }

  std::vector<std::byte> &content =
      mode == FileMode::kWrite ? world_->truncate_file(path) : world_->open_file(path);
  return std::make_unique<SimFile>(*world_, content, mode);
}

core::expected<std::unique_ptr<IProcess>>
SimPlatform::spawn_process(const ProcessConfig &config) noexcept {
  const std::optional<ProcessExit> outcome = world_->find_program(config.executable);
  if (!outcome.has_value()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  const std::int32_t identifier = world_->next_process_id();
  world_->record("process.spawn", static_cast<std::uint64_t>(identifier));
  return std::make_unique<SimProcess>(*world_, identifier, *outcome);
}

std::int32_t SimPlatform::current_process_id() const noexcept {
  return detail::SimWorld::kSelfProcessId;
}

bool SimPlatform::process_alive(std::int32_t identifier) const noexcept {
  return world_->process_alive(identifier);
}

core::expected<std::unique_ptr<IWatchdogDevice>>
SimPlatform::open_watchdog(std::string_view path) noexcept {
  if (!world_->is_watchdog_path(path)) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return std::make_unique<SimWatchdogDevice>(*world_);
}

core::expected<void> SimPlatform::lock_memory() noexcept {
  world_->record("platform.lock_memory", 0);
  return {};
}

core::expected<void>
SimPlatform::set_current_thread_deadline(const DeadlineParameters &parameters) noexcept {
  // The same shape the sporadic task model requires, checked identically to
  // the POSIX backend so a configuration mistake is caught in simulation
  // rather than only on the target.
  const bool positive =
      parameters.runtime.ns() > 0 && parameters.deadline.ns() > 0 && parameters.period.ns() > 0;
  if (!positive || parameters.runtime.ns() > parameters.deadline.ns() ||
      parameters.deadline.ns() > parameters.period.ns()) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  // No admission test: a simulated world runs one body at a time to
  // completion, so there is no bandwidth to compete for. What the simulation
  // does reproduce is the contract - a well formed reservation is accepted,
  // a malformed one is refused.
  world_->record("platform.set_deadline", static_cast<std::uint64_t>(parameters.runtime.ns()));
  return {};
}

core::expected<void> SimPlatform::set_current_thread_scheduling(SchedulingPolicy policy,
                                                                core::Priority priority) noexcept {
  const core::expected<void> valid = validate_scheduling(policy, priority);
  if (!valid.has_value()) {
    return std::unexpected{valid.error()};
  }
  world_->record("platform.set_scheduling", priority.value());
  return {};
}

void SimPlatform::register_program(std::string_view path, ProcessExit outcome) {
  world_->register_program(path, outcome);
}

void SimPlatform::set_watchdog_path(std::string_view path) { world_->set_watchdog_path(path); }

std::uint64_t SimPlatform::event_digest() const noexcept { return world_->event_digest(); }

} // namespace volt::pal::sim
