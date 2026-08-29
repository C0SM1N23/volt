#pragma once

#include "allocator.hpp"
#include "duration.hpp"
#include "environment_sink.hpp"
#include "timer_scheduler.hpp"
#include "timestamp.hpp"

#include <cstdint>

namespace volt::actor {

/// Owns deterministic environment state shared by one concrete backend.
/// @thread the actor's single dispatcher thread
/// @rt     allocation-free after construction
class EnvironmentContext final {
public:
  /// Binds bounded runtime services and starts a reproducible random stream.
  /// @pre    all referenced objects outlive the context and its environment
  /// @thread initialization thread
  /// @rt     allocation-free and O(1)
  EnvironmentContext(TimerScheduler &timers, EnvironmentSink &sink, Allocator &allocator,
                     std::uint64_t random_seed) noexcept;

  /// Schedules a timer relative to the supplied monotonic time.
  /// @pre    `delay` is non-negative
  /// @post   returns zero only when timer capacity is exhausted
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by the timer scheduler
  [[nodiscard]] TimerId set_timer(Timestamp monotonic_now, Duration delay, TimerTag tag) noexcept;

  /// Cancels one pending timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by the timer scheduler
  void cancel_timer(TimerId timer) noexcept;

  /// Publishes through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  void publish(TopicId topic, PayloadView payload) noexcept;

  /// Assigns and submits one request through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @post   returns a unique nonzero identifier within this context
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  [[nodiscard]] RequestId call(ServiceId service, MethodId method, PayloadView payload,
                               Duration timeout) noexcept;

  /// Responds through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  void respond(RequestId request, PayloadView payload) noexcept;

  /// Returns the next SplitMix64 value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t random() noexcept;

  /// Logs through the injected effect sink.
  /// @pre    `message` and `args` remain alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  void log(Level level, std::string_view message, LogArgs args) noexcept;

  /// Traces through the injected effect sink.
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  void trace(TraceEventId event, std::uint64_t arg) noexcept;

  /// Returns the caller-owned actor arena.
  /// @post   ownership remains with runtime initialization
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] Allocator &allocator() noexcept;

private:
  TimerScheduler *timers_;
  EnvironmentSink *sink_;
  Allocator *allocator_;
  std::uint64_t random_state_;
  std::uint64_t next_request_id_ = 1;
};

} // namespace volt::actor
