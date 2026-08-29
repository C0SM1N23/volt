#pragma once

#include "mailbox_full_policy.hpp"
#include "mailbox_stats.hpp"

#include "volt/actor/message.hpp"
#include "volt/core/error.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace volt::actor {

/// Holds messages in a fixed-capacity FIFO owned by one dispatcher.
/// @thread the actor's single dispatcher thread
/// @rt     allocation-free and O(1)
template <std::size_t N> class Mailbox final {
  static_assert(N > 0, "a mailbox needs at least one slot");

public:
  /// Constructs an empty mailbox with a fixed full policy.
  /// @thread initialization thread
  /// @rt     allocation-free and O(1)
  explicit constexpr Mailbox(MailboxFullPolicy full_policy) noexcept : full_policy_{full_policy} {}

  /// Enqueues one message or applies the configured full policy.
  /// @pre    `message.payload` remains alive and unmoved until its delivery completes
  /// @post   DROP_OLDEST always retains the new message; DROP_NEW retains the old queue;
  ///         FAULT retains the old queue and reports exhaustion
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kTransientMessageLost for DROP_NEW, kResourceExhausted for FAULT
  [[nodiscard]] expected<void> push(const Message &message) noexcept {
    if (size_ == N) {
      const expected<void> full_result = apply_full_policy();
      if (!full_result.has_value()) {
        return full_result;
      }
    }

    slots_[physical_index(size_)] = message;
    ++size_;
    ++stats_.accepted;
    return {};
  }

  /// Returns the oldest message without consuming it.
  /// @pre    the sender still owns and preserves the payload bytes
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kResourceUnavailable when empty
  [[nodiscard]] expected<Message> peek() const noexcept {
    if (size_ == 0) {
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }
    return slots_[head_];
  }

  /// Removes and returns the oldest message.
  /// @pre    the sender still owns and preserves the payload bytes through delivery
  /// @post   one slot becomes available on success
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kResourceUnavailable when empty
  [[nodiscard]] expected<Message> pop() noexcept {
    const expected<Message> front = peek();
    if (!front.has_value()) {
      return std::unexpected{front.error()};
    }
    head_ = (head_ + 1U) % N;
    --size_;
    return *front;
  }

  /// Returns the compile-time slot capacity.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns the number of queued messages.
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  /// Reports whether no message is queued.
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  /// Returns the immutable full policy selected at construction.
  [[nodiscard]] MailboxFullPolicy full_policy() const noexcept { return full_policy_; }

  /// Returns a snapshot of all mailbox outcome counters.
  /// @thread the actor's single dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] MailboxStats stats() const noexcept { return stats_; }

private:
  [[nodiscard]] constexpr std::size_t physical_index(std::size_t logical_index) const noexcept {
    return (head_ + logical_index) % N;
  }

  [[nodiscard]] expected<void> apply_full_policy() noexcept {
    switch (full_policy_) {
    case MailboxFullPolicy::kDropOldest:
      head_ = (head_ + 1U) % N;
      --size_;
      ++stats_.dropped_oldest;
      return {};
    case MailboxFullPolicy::kDropNew:
      ++stats_.dropped_new;
      return std::unexpected{core::ErrorCode::kTransientMessageLost};
    case MailboxFullPolicy::kFault:
      ++stats_.faults;
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }
    VOLT_ASSERT(false, "mailbox full policy left its closed enum");
    std::unreachable();
  }

  std::array<Message, N> slots_{};
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  MailboxFullPolicy full_policy_;
  MailboxStats stats_{};
};

} // namespace volt::actor
