#pragma once

#include "timer_event.hpp"

#include "volt/core/error.hpp"

#include <optional>

namespace volt::actor {

/// Supplies bounded timers to an environment and its dispatcher.
class TimerScheduler {
public:
  /// Destroys a scheduler through its interface.
  virtual ~TimerScheduler() = default;

  /// Adds one timer at an absolute monotonic deadline.
  /// @post   returns zero and counts the failure when capacity is exhausted
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded
  [[nodiscard]] virtual TimerId schedule(Timestamp deadline, TimerTag tag) noexcept = 0;

  /// Cancels one pending timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded
  virtual void cancel(TimerId timer) noexcept = 0;

  /// Returns the earliest pending timer without consuming it.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] virtual std::optional<TimerEvent> peek() const noexcept = 0;

  /// Removes and returns the earliest pending timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded
  /// @errors kResourceUnavailable when no timer is pending
  [[nodiscard]] virtual expected<TimerEvent> pop() noexcept = 0;
};

} // namespace volt::actor
