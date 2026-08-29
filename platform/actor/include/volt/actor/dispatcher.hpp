#pragma once

#include "dispatch_report.hpp"
#include "mailbox.hpp"
#include "timer_scheduler.hpp"

#include "volt/actor/i_actor.hpp"

#include <cstddef>
#include <optional>

namespace volt::actor {

/// Delivers bounded mailbox and timer work on one deterministic actor thread.
/// @satisfies REQ-PLT-030
/// @thread exactly one owning thread
/// @rt     allocation-free and bounded by `MaxEventsPerRun`
template <std::size_t MailboxCapacity, std::size_t MaxEventsPerRun> class Dispatcher final {
  static_assert(MaxEventsPerRun > 0, "a dispatcher pass needs a positive event budget");

public:
  /// Binds one actor, environment, timer scheduler, and mailbox policy.
  /// @pre    every referenced object outlives the dispatcher
  /// @thread initialization thread
  /// @rt     allocation-free and O(1)
  Dispatcher(IActor &actor, Environment &environment, TimerScheduler &timers,
             MailboxFullPolicy full_policy) noexcept
      : actor_{&actor}, environment_{&environment}, timers_{&timers}, mailbox_{full_policy} {}

  /// Starts the actor exactly once.
  /// @pre    the dispatcher has never been started
  /// @post   lifecycle callbacks may receive events
  /// @thread the owning dispatcher thread
  /// @rt     actor-defined
  void start() {
    VOLT_ASSERT(lifecycle_ == Lifecycle::kCreated, "actor dispatcher started twice");
    lifecycle_ = Lifecycle::kStarted;
    actor_->on_start(*environment_);
  }

  /// Stops the actor exactly once.
  /// @pre    the dispatcher is started
  /// @post   no event may be dispatched until a new dispatcher is constructed
  /// @thread the owning dispatcher thread
  /// @rt     actor-defined
  void stop() noexcept {
    VOLT_ASSERT(lifecycle_ == Lifecycle::kStarted, "actor dispatcher stopped before start");
    actor_->on_stop(*environment_);
    lifecycle_ = Lifecycle::kStopped;
  }

  /// Queues one actor message.
  /// @pre    `message.payload` remains alive and unmoved until delivery completes
  /// @thread the owning dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kTransientMessageLost or kResourceExhausted under the configured policy
  [[nodiscard]] expected<void> enqueue(const Message &message) noexcept {
    return mailbox_.push(message);
  }

  /// Delivers due work in deterministic timestamp order.
  ///
  /// A message wins a timestamp tie because it may cancel or replace a timer
  /// based on newer external state. Firing the timer first would let stale
  /// state act even though its correcting input had already arrived.
  ///
  /// @pre    `start` completed and queued messages are in nondecreasing timestamp order
  /// @post   at most `MaxEventsPerRun` callbacks ran; each event at or before
  ///         `ready_through` preceded every later event
  /// @thread the owning dispatcher thread
  /// @rt     allocation-free and bounded by `MaxEventsPerRun`
  [[nodiscard]] DispatchReport run_ready(Timestamp ready_through) {
    VOLT_ASSERT(lifecycle_ == Lifecycle::kStarted, "actor dispatcher ran before start");
    VOLT_LOOP_BOUND(MaxEventsPerRun);

    DispatchReport report{};
    for (std::size_t count = 0; count < MaxEventsPerRun; ++count) {
      const std::optional<bool> choice = choose_message(ready_through);
      if (!choice.has_value()) {
        return report;
      }
      dispatch_selected(*choice, report);
    }
    report.budget_exhausted = has_due_event(ready_through);
    return report;
  }

  /// Returns the mailbox counters without exposing its mutable queue.
  /// @thread the owning dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] MailboxStats mailbox_stats() const noexcept { return mailbox_.stats(); }

  /// Returns how many messages await delivery.
  /// @thread the owning dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t pending_messages() const noexcept { return mailbox_.size(); }

private:
  enum class Lifecycle : std::uint8_t { kCreated, kStarted, kStopped };

  [[nodiscard]] std::optional<bool> choose_message(Timestamp ready_through) const noexcept {
    const expected<Message> message = mailbox_.peek();
    const std::optional<TimerEvent> timer = timers_->peek();
    const bool message_due = message.has_value() && message->timestamp <= ready_through;
    const bool timer_due = timer.has_value() && timer->deadline <= ready_through;
    if (!message_due && !timer_due) {
      return std::nullopt;
    }
    if (!message_due) {
      return false;
    }
    return !timer_due || message->timestamp <= timer->deadline;
  }

  void dispatch_selected(bool is_message, DispatchReport &report) {
    if (is_message) {
      const expected<Message> message = mailbox_.pop();
      VOLT_ASSERT(message.has_value(), "selected mailbox message disappeared");
      actor_->on_message(*message, *environment_);
      ++report.messages;
      return;
    }

    const expected<TimerEvent> timer = timers_->pop();
    VOLT_ASSERT(timer.has_value(), "selected actor timer disappeared");
    actor_->on_timer(timer->id, timer->tag, *environment_);
    ++report.timers;
  }

  [[nodiscard]] bool has_due_event(Timestamp ready_through) const noexcept {
    return choose_message(ready_through).has_value();
  }

  IActor *actor_;
  Environment *environment_;
  TimerScheduler *timers_;
  Mailbox<MailboxCapacity> mailbox_;
  Lifecycle lifecycle_ = Lifecycle::kCreated;
};

} // namespace volt::actor
