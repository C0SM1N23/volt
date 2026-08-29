#pragma once

#include "timer_scheduler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace volt::actor {

/// Stores actor timers in a fixed-capacity deterministic min-heap.
/// @thread the actor's dispatcher thread
/// @rt     allocation-free; schedule/pop are O(log N), cancellation is O(N)
template <std::size_t N> class TimerQueue final : public TimerScheduler {
  static_assert(N > 0, "a timer queue needs at least one slot");

public:
  /// Constructs an empty timer queue.
  constexpr TimerQueue() noexcept = default;

  /// Adds one timer ordered by deadline and then identifier.
  /// @post   returns zero and increments `schedule_failures` when full
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(log N)
  [[nodiscard]] TimerId schedule(Timestamp deadline, TimerTag tag) noexcept override {
    if (size_ == N) {
      ++schedule_failures_;
      return TimerId{};
    }

    VOLT_ASSERT(next_id_ != std::numeric_limits<std::uint64_t>::max(),
                "timer identifier space exhausted");
    const TimerId timer{next_id_};
    ++next_id_;
    heap_[size_] = TimerEvent{.deadline = deadline, .id = timer, .tag = tag};
    sift_up(size_);
    ++size_;
    return timer;
  }

  /// Cancels a timer while preserving heap order.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(N)
  void cancel(TimerId timer) noexcept override {
    const std::optional<std::size_t> found = find(timer);
    if (!found.has_value()) {
      ++cancel_misses_;
      return;
    }

    --size_;
    if (*found == size_) {
      return;
    }
    heap_[*found] = heap_[size_];
    if (*found > 0 && earlier(heap_[*found], heap_[parent(*found)])) {
      sift_up(*found);
    } else {
      sift_down(*found);
    }
  }

  /// Returns the earliest timer without consuming it.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::optional<TimerEvent> peek() const noexcept override {
    if (size_ == 0) {
      return std::nullopt;
    }
    return heap_[0];
  }

  /// Removes the earliest timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(log N)
  /// @errors kResourceUnavailable when empty
  [[nodiscard]] expected<TimerEvent> pop() noexcept override {
    if (size_ == 0) {
      ++pop_failures_;
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }

    const TimerEvent result = heap_[0];
    --size_;
    if (size_ != 0) {
      heap_[0] = heap_[size_];
      sift_down(0);
    }
    return result;
  }

  /// Returns the compile-time timer capacity.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns the number of pending timers.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  /// Returns how many schedules found the queue full.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t schedule_failures() const noexcept { return schedule_failures_; }

  /// Returns how many cancellations named no pending timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t cancel_misses() const noexcept { return cancel_misses_; }

  /// Returns how many pops found no timer.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t pop_failures() const noexcept { return pop_failures_; }

private:
  [[nodiscard]] static constexpr bool earlier(const TimerEvent &lhs,
                                              const TimerEvent &rhs) noexcept {
    return lhs.deadline < rhs.deadline || (lhs.deadline == rhs.deadline && lhs.id < rhs.id);
  }

  [[nodiscard]] static constexpr std::size_t parent(std::size_t index) noexcept {
    return (index - 1U) / 2U;
  }

  [[nodiscard]] static constexpr std::size_t left_child(std::size_t index) noexcept {
    return (2U * index) + 1U;
  }

  [[nodiscard]] std::optional<std::size_t> find(TimerId timer) const noexcept {
    VOLT_LOOP_BOUND(N);
    for (std::size_t index = 0; index < size_; ++index) {
      if (heap_[index].id == timer) {
        return index;
      }
    }
    return std::nullopt;
  }

  void sift_up(std::size_t index) noexcept {
    VOLT_LOOP_BOUND(N);
    while (index > 0) {
      const std::size_t parent_index = parent(index);
      if (!earlier(heap_[index], heap_[parent_index])) {
        return;
      }
      std::swap(heap_[index], heap_[parent_index]);
      index = parent_index;
    }
  }

  void sift_down(std::size_t index) noexcept {
    VOLT_LOOP_BOUND(N);
    while (left_child(index) < size_) {
      const std::size_t left = left_child(index);
      const std::size_t right = left + 1U;
      const std::size_t earliest =
          right < size_ && earlier(heap_[right], heap_[left]) ? right : left;
      if (!earlier(heap_[earliest], heap_[index])) {
        return;
      }
      std::swap(heap_[index], heap_[earliest]);
      index = earliest;
    }
  }

  std::array<TimerEvent, N> heap_{};
  std::size_t size_ = 0;
  std::uint64_t next_id_ = 1;
  std::uint64_t schedule_failures_ = 0;
  std::uint64_t cancel_misses_ = 0;
  std::uint64_t pop_failures_ = 0;
};

} // namespace volt::actor
