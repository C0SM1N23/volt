#pragma once

#include "detail/cache_line.hpp"
#include "detail/trivial_storage.hpp"

#include "volt/core/error.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace volt::memory {

/// Passes fixed-size values from many producers to one consumer without blocking.
///
/// Each slot carries the sequence from Vyukov's bounded queue. Producers use a
/// single compare-exchange attempt: contention is reported as `kResourceBusy`
/// so a real-time caller retains a hard operation bound while system-wide
/// progress remains lock-free.
///
/// @thread any producer calls `try_push`; one consumer calls `try_pop`
/// @rt     allocation-free and lock-free
template <typename T, std::size_t N> class MpscBoundedQueue final {
  static_assert(N >= 2, "a bounded queue needs at least two slots");
  static_assert((N & (N - 1U)) == 0, "queue capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>, "queue values must be trivially copyable");

  class Cell final {
  public:
    std::atomic<std::size_t> sequence{0};
    detail::TrivialStorage<T> payload{};
  };

public:
  /// Constructs an empty queue with each slot in its initial sequence.
  MpscBoundedQueue() noexcept {
    for (std::size_t index = 0; index < N; ++index) {
      cells_[index].sequence.store(index, std::memory_order_relaxed);
    }
  }

  // Rule of five because atomics have one synchronization address and each
  // endpoint is bound to this queue's cell storage.
  ~MpscBoundedQueue() noexcept = default;
  MpscBoundedQueue(const MpscBoundedQueue &) = delete;
  MpscBoundedQueue &operator=(const MpscBoundedQueue &) = delete;
  MpscBoundedQueue(MpscBoundedQueue &&) = delete;
  MpscBoundedQueue &operator=(MpscBoundedQueue &&) = delete;

  /// Publishes one value when a slot can be claimed immediately.
  /// @post   on success the single consumer eventually observes an equal value
  /// @thread any producer
  /// @rt     allocation-free, lock-free and O(1)
  /// @errors kResourceExhausted when full; kResourceBusy when another producer wins
  [[nodiscard]] core::expected<void> try_push(const T &value) noexcept {
    std::size_t position = producer_position_.load(std::memory_order_relaxed);
    Cell &cell = cells_[position & kIndexMask];
    // Acquire pairs with the consumer's release of this cell, preventing a
    // producer from overwriting payload bytes the consumer still reads.
    const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
    if (sequence != position) {
      count_push_failure();
      const core::ErrorCode code = sequence < position ? core::ErrorCode::kResourceExhausted
                                                       : core::ErrorCode::kResourceBusy;
      return std::unexpected{code};
    }

    // Relaxed is sufficient for reservation: the cell sequence, not this
    // cursor, releases and acquires the payload between endpoints.
    if (!producer_position_.compare_exchange_strong(
            position, position + 1U, std::memory_order_relaxed, std::memory_order_relaxed)) {
      count_push_failure();
      return std::unexpected{core::ErrorCode::kResourceBusy};
    }

    cell.payload.store(value);
    // Release publishes the payload to the consumer's acquire sequence load.
    cell.sequence.store(position + 1U, std::memory_order_release);
    return {};
  }

  /// Takes the oldest fully published value.
  /// @post   on success one slot becomes available to a producer
  /// @thread the single consumer
  /// @rt     allocation-free and wait-free
  /// @errors kResourceUnavailable when no complete value is ready
  [[nodiscard]] core::expected<T> try_pop() noexcept {
    Cell &cell = cells_[consumer_position_ & kIndexMask];
    // Acquire pairs with the winning producer's release, so its payload is
    // complete before the consumer copies it.
    const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
    if (sequence != consumer_position_ + 1U) {
      pop_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }

    const T value = cell.payload.load();
    // Release makes the completed payload copy precede the acquire load by
    // the producer that next reuses this cell.
    cell.sequence.store(consumer_position_ + N, std::memory_order_release);
    ++consumer_position_;
    return value;
  }

  /// Returns the compile-time number of usable slots.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns how many pushes found fullness or producer contention.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t push_failures() const noexcept {
    return push_failures_.load(std::memory_order_relaxed);
  }

  /// Returns how many pops found no complete value.
  /// @thread any observer; one consumer updates it
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t pop_failures() const noexcept {
    return pop_failures_.load(std::memory_order_relaxed);
  }

private:
  static constexpr std::size_t kIndexMask = N - 1U;

  void count_push_failure() noexcept { push_failures_.fetch_add(1U, std::memory_order_relaxed); }

  std::array<Cell, N> cells_{};

  // All producers contend here; the per-cell sequence publishes actual data.
  alignas(detail::kCacheLineBytes) std::atomic<std::size_t> producer_position_{0};
  // Only the consumer modifies this cursor, so making it atomic would add
  // coherency traffic without creating synchronization.
  alignas(detail::kCacheLineBytes) std::size_t consumer_position_ = 0;
  // Producers jointly update this diagnostic; it orders no queue data.
  alignas(detail::kCacheLineBytes) std::atomic<std::uint64_t> push_failures_{0};
  // The consumer owns this diagnostic; observers read snapshots.
  alignas(detail::kCacheLineBytes) std::atomic<std::uint64_t> pop_failures_{0};
};

} // namespace volt::memory
