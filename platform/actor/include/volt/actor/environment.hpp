#pragma once

#include "allocator.hpp"
#include "duration.hpp"
#include "level.hpp"
#include "log_args.hpp"
#include "method_id.hpp"
#include "payload_view.hpp"
#include "request_id.hpp"
#include "service_id.hpp"
#include "timer_id.hpp"
#include "timer_tag.hpp"
#include "timestamp.hpp"
#include "topic_id.hpp"
#include "trace_event_id.hpp"

#include <cstdint>
#include <string_view>

namespace volt {

/// Restricts every actor interaction with the world to deterministic inputs.
/// @satisfies REQ-PLT-030
class Environment {
public:
  /// Destroys the environment through its interface.
  virtual ~Environment() = default;

  /// Returns globally meaningful time.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free
  [[nodiscard]] virtual Timestamp now() const noexcept = 0;

  /// Returns local monotonic time.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free
  [[nodiscard]] virtual Timestamp mono() const noexcept = 0;

  /// Schedules one actor timer relative to monotonic time.
  /// @pre    `d` is non-negative
  /// @post   returns zero only when the bounded timer capacity is exhausted
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded
  [[nodiscard]] virtual TimerId set_timer(Duration delay, TimerTag tag) = 0;

  /// Cancels a timer when it is still pending.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded
  virtual void cancel_timer(TimerId timer) noexcept = 0;

  /// Publishes immutable payload bytes through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  virtual void publish(TopicId topic, PayloadView payload) = 0;

  /// Starts a request through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @post   returns a nonzero request identifier
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  [[nodiscard]] virtual RequestId call(ServiceId service, MethodId method, PayloadView payload,
                                       Duration timeout) = 0;

  /// Sends a response through the injected effect sink.
  /// @pre    `payload` remains alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  virtual void respond(RequestId request, PayloadView payload) = 0;

  /// Returns the next deterministic random value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] virtual std::uint64_t random() noexcept = 0;

  /// Emits a structured actor log through the injected effect sink.
  /// @pre    `message` and `args` remain alive through the call; nothing is retained here
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  virtual void log(Level level, std::string_view message, LogArgs args) noexcept = 0;

  /// Emits one fixed-width trace event through the injected effect sink.
  /// @thread the actor's dispatcher thread
  /// @rt     determined by the injected sink
  virtual void trace(TraceEventId event, std::uint64_t arg) noexcept = 0;

  /// Returns the caller-backed allocator assigned to this actor.
  /// @post   the allocator remains owned by runtime initialization
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] virtual Allocator &allocator() noexcept = 0;
};

} // namespace volt
