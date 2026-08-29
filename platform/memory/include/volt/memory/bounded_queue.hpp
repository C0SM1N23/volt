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

/// Passes fixed-size values from one producer to one consumer without blocking.
///
/// Monotonic cursors follow the bounded SPSC ring described by Vyukov. They
/// occupy separate cache lines so ownership traffic does not cause false
/// sharing between the producer and consumer.
///
/// @thread one producer calls `try_push`; one consumer calls `try_pop`
/// @rt     allocation-free and wait-free
template <typename T, std::size_t N> class BoundedQueue final {
  static_assert(N >= 2, "a bounded queue needs at least two slots");
  static_assert((N & (N - 1U)) == 0, "queue capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>, "queue values must be trivially copyable");

public:
  /// Constructs an empty queue.
  constexpr BoundedQueue() noexcept = default;

  // Rule of five because cursors participate in a synchronization order and
  // each endpoint is bound to this queue's storage address.
  ~BoundedQueue() noexcept = default;
  BoundedQueue(const BoundedQueue &) = delete;
  BoundedQueue &operator=(const BoundedQueue &) = delete;
  BoundedQueue(BoundedQueue &&) = delete;
  BoundedQueue &operator=(BoundedQueue &&) = delete;

  /// Publishes one value when capacity is available.
  /// @post   on success the consumer eventually observes an equal value
  /// @thread the single producer
  /// @rt     allocation-free and wait-free
  /// @errors kResourceExhausted when the queue is full
  [[nodiscard]] core::expected<void> try_push(const T &value) noexcept {
    const std::size_t producer = producer_position_.load(std::memory_order_relaxed);
    // Acquire pairs with the consumer release, so its slot read completes
    // before the producer is allowed to overwrite that slot.
    const std::size_t consumer = consumer_position_.load(std::memory_order_acquire);
    if (producer - consumer == N) {
      push_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }

    slots_[producer & kIndexMask].store(value);
    // Release makes the payload visible before the cursor that publishes it,
    // the consumer's acquire load observes both in that order.
    producer_position_.store(producer + 1U, std::memory_order_release);
    return {};
  }

  /// Takes the oldest published value.
  /// @post   on success one slot becomes available to the producer
  /// @thread the single consumer
  /// @rt     allocation-free and wait-free
  /// @errors kResourceUnavailable when the queue is empty
  [[nodiscard]] core::expected<T> try_pop() noexcept {
    const std::size_t consumer = consumer_position_.load(std::memory_order_relaxed);
    // Acquire pairs with the producer release so the payload is complete
    // before this thread copies it out of the slot.
    const std::size_t producer = producer_position_.load(std::memory_order_acquire);
    if (consumer == producer) {
      pop_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }

    const T value = slots_[consumer & kIndexMask].load();
    // Release ensures the payload copy completes before the producer's acquire
    // permits that slot to be reused.
    consumer_position_.store(consumer + 1U, std::memory_order_release);
    return value;
  }

  /// Returns the compile-time number of usable slots.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns how many pushes found the queue full.
  /// @thread any observer; one producer updates it
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t push_failures() const noexcept {
    return push_failures_.load(std::memory_order_relaxed);
  }

  /// Returns how many pops found the queue empty.
  /// @thread any observer; one consumer updates it
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t pop_failures() const noexcept {
    return pop_failures_.load(std::memory_order_relaxed);
  }

private:
  static constexpr std::size_t kIndexMask = N - 1U;

  std::array<detail::TrivialStorage<T>, N> slots_{};

  // The producer is the sole modifier; the consumer only observes it.
  alignas(detail::kCacheLineBytes) std::atomic<std::size_t> producer_position_{0};
  // The consumer is the sole modifier; the producer only observes it.
  alignas(detail::kCacheLineBytes) std::atomic<std::size_t> consumer_position_{0};
  // Each endpoint owns its failure counter; observers load relaxed snapshots.
  alignas(detail::kCacheLineBytes) std::atomic<std::uint64_t> push_failures_{0};
  alignas(detail::kCacheLineBytes) std::atomic<std::uint64_t> pop_failures_{0};
};

} // namespace volt::memory
