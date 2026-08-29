#pragma once

#include "environment.hpp"
#include "environment_context.hpp"

#include "volt/pal/sim/sim_platform.hpp"

namespace volt::actor {

/// Implements actor inputs from the deterministic simulation PAL.
class SimEnvironment final : public Environment {
public:
  /// Binds the virtual clock and deterministic actor runtime state.
  /// @pre    `platform` and `context` outlive this environment
  /// @thread initialization thread
  /// @rt     allocation-free and O(1)
  SimEnvironment(pal::sim::SimPlatform &platform, EnvironmentContext &context) noexcept;

  /// Returns simulated realtime through PAL.
  [[nodiscard]] Timestamp now() const noexcept override;

  /// Returns simulated monotonic time through PAL.
  [[nodiscard]] Timestamp mono() const noexcept override;

  /// Schedules one bounded actor timer.
  [[nodiscard]] TimerId set_timer(Duration delay, TimerTag tag) noexcept override;

  /// Cancels one bounded actor timer.
  void cancel_timer(TimerId timer) noexcept override;

  /// Publishes through the injected effect sink.
  void publish(TopicId topic, PayloadView payload) noexcept override;

  /// Starts a request through the injected effect sink.
  [[nodiscard]] RequestId call(ServiceId service, MethodId method, PayloadView payload,
                               Duration timeout) noexcept override;

  /// Responds through the injected effect sink.
  void respond(RequestId request, PayloadView payload) noexcept override;

  /// Returns the next deterministic random value.
  [[nodiscard]] std::uint64_t random() noexcept override;

  /// Logs through the injected effect sink.
  void log(Level level, std::string_view message, LogArgs args) noexcept override;

  /// Traces through the injected effect sink.
  void trace(TraceEventId event, std::uint64_t arg) noexcept override;

  /// Returns the caller-owned actor allocator.
  [[nodiscard]] Allocator &allocator() noexcept override;

private:
  pal::sim::SimPlatform *platform_;
  EnvironmentContext *context_;
};

} // namespace volt::actor
