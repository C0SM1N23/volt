#pragma once

#include "pool_index.hpp"

#include "volt/core/error.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace volt::memory {

/// Owns a fixed set of slots through an atomic, tagged, index-based free list.
///
/// The tag prevents ABA when a slot is released and reclaimed between another
/// thread's head load and compare-exchange. A failed compare-exchange reports
/// `kResourceBusy`, keeping each call bounded while preserving lock-freedom.
///
/// @thread any; an allocated slot has one owner until release
/// @rt     allocation-free and O(1)
template <typename T, std::size_t N> class AtomicFixedPool final {
  static_assert(N > 0, "a fixed pool needs at least one slot");
  static_assert(N < std::numeric_limits<std::uint32_t>::max(), "pool indices must fit uint32");
  static_assert(std::is_trivially_copyable_v<T>, "shared-memory pool values must be trivial");
  static_assert(std::is_default_constructible_v<T>, "pool slots must be default constructible");

public:
  /// Initializes every slot as available.
  AtomicFixedPool() noexcept {
    for (std::size_t index = 0; index < N; ++index) {
      next_[index].store(static_cast<std::uint32_t>(index + 1U), std::memory_order_relaxed);
    }
    head_.store(pack(0U, 0U), std::memory_order_relaxed);
  }

  // Rule of five because atomics have one address in the synchronization
  // order and outstanding PoolIndex values designate this exact object.
  ~AtomicFixedPool() noexcept = default;
  AtomicFixedPool(const AtomicFixedPool &) = delete;
  AtomicFixedPool &operator=(const AtomicFixedPool &) = delete;
  AtomicFixedPool(AtomicFixedPool &&) = delete;
  AtomicFixedPool &operator=(AtomicFixedPool &&) = delete;

  /// Claims one slot by its relocatable index.
  /// @post   the returned index remains owned until `release` succeeds
  /// @thread any
  /// @rt     allocation-free, lock-free and O(1)
  /// @errors kResourceExhausted when every slot is owned; kResourceBusy on contention
  [[nodiscard]] core::expected<PoolIndex> allocate() noexcept {
    std::uint64_t observed = head_.load(std::memory_order_acquire);
    const std::uint32_t index = unpack_index(observed);
    if (index == sentinel()) {
      count_allocation_failure();
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }

    // Relaxed is sufficient because the acquire head load already imported
    // the releasing thread's link publication.
    const std::uint32_t next = next_[index].load(std::memory_order_relaxed);
    const std::uint64_t desired = pack(next, unpack_tag(observed) + 1U);
    // Acquire reads the released `next_` link; release publishes the new head
    // to competing allocators. A failed acquire refreshes `observed` safely.
    if (!head_.compare_exchange_strong(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      count_allocation_failure();
      return std::unexpected{core::ErrorCode::kResourceBusy};
    }

    // The successful head acquire already imported the prior owner's writes,
    // this relaxed marker only checks the free-list invariant.
    const bool was_allocated = allocated_[index].exchange(true, std::memory_order_relaxed);
    VOLT_ASSERT(!was_allocated, "a free-list slot was already marked allocated");
    available_.fetch_sub(1U, std::memory_order_relaxed);
    return PoolIndex{index};
  }

  /// Returns a slot to the free list.
  /// @pre    `index` was returned by this pool and has not already been released
  /// @post   the slot may be claimed by a later `allocate`
  /// @thread any
  /// @rt     allocation-free, lock-free and O(1)
  /// @errors kInternalOutOfRange for a foreign or free index; kResourceBusy on contention
  [[nodiscard]] core::expected<void> release(PoolIndex index) noexcept {
    const std::uint32_t value = index.value();
    if (value >= N || !mark_releasing(value)) {
      release_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }

    std::uint64_t observed = head_.load(std::memory_order_relaxed);
    // The following release CAS publishes this relaxed link store. Keeping the
    // link atomic also makes a losing stale allocator's speculative read safe.
    next_[value].store(unpack_index(observed), std::memory_order_relaxed);
    const std::uint64_t desired = pack(value, unpack_tag(observed) + 1U);
    // Release publishes the slot value and link to the next allocator. A
    // failed call retains ownership, so its refreshed head needs no acquire.
    if (!head_.compare_exchange_strong(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed)) {
      allocated_[value].store(true, std::memory_order_relaxed);
      release_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kResourceBusy};
    }

    available_.fetch_add(1U, std::memory_order_relaxed);
    return {};
  }

  /// Returns the mutable value in an owned slot.
  /// @pre    only the slot owner uses the reference, and not after release
  /// @thread the slot owner
  /// @rt     allocation-free and O(1)
  /// @errors kInternalOutOfRange when the index is foreign or not owned
  [[nodiscard]] core::expected<std::reference_wrapper<T>> get(PoolIndex index) noexcept {
    if (!owns(index)) {
      access_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }
    return std::ref(values_[index.value()]);
  }

  /// Returns the immutable value in an owned slot.
  /// @pre    the reference is not retained after release
  /// @thread any reader synchronized by the slot owner
  /// @rt     allocation-free and O(1)
  /// @errors kInternalOutOfRange when the index is foreign or not owned
  [[nodiscard]] core::expected<std::reference_wrapper<const T>>
  get(PoolIndex index) const noexcept {
    if (!owns(index)) {
      access_failures_.fetch_add(1U, std::memory_order_relaxed);
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }
    return std::cref(values_[index.value()]);
  }

  /// Returns the compile-time slot capacity.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns a concurrent snapshot of the number of immediately available slots.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t available() const noexcept {
    return available_.load(std::memory_order_relaxed);
  }

  /// Returns how many allocations found exhaustion or contention.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t allocation_failures() const noexcept {
    return allocation_failures_.load(std::memory_order_relaxed);
  }

  /// Returns how many releases rejected an index or lost contention.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t release_failures() const noexcept {
    return release_failures_.load(std::memory_order_relaxed);
  }

  /// Returns how many accesses rejected a foreign or free index.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t access_failures() const noexcept {
    return access_failures_.load(std::memory_order_relaxed);
  }

private:
  // The low half carries the shared-memory index and the high half is the ABA
  // tag. Their 32-bit widths come from PoolIndex; changing either changes the
  // packed shared representation and requires compatibility review.
  static constexpr unsigned kTagShiftBits = 32;
  static constexpr std::uint64_t kIndexMask = 0xFFFF'FFFFULL;

  [[nodiscard]] static constexpr std::uint32_t sentinel() noexcept {
    return static_cast<std::uint32_t>(N);
  }

  [[nodiscard]] static constexpr std::uint64_t pack(std::uint32_t index,
                                                    std::uint32_t tag) noexcept {
    return (static_cast<std::uint64_t>(tag) << kTagShiftBits) | index;
  }

  [[nodiscard]] static constexpr std::uint32_t unpack_index(std::uint64_t head) noexcept {
    return static_cast<std::uint32_t>(head & kIndexMask);
  }

  [[nodiscard]] static constexpr std::uint32_t unpack_tag(std::uint64_t head) noexcept {
    return static_cast<std::uint32_t>(head >> kTagShiftBits);
  }

  [[nodiscard]] bool mark_releasing(std::uint32_t index) noexcept {
    bool expected = true;
    // The head's later release publishes slot data; this marker only rejects
    // duplicate releases and therefore orders no payload access.
    return allocated_[index].compare_exchange_strong(expected, false, std::memory_order_relaxed,
                                                     std::memory_order_relaxed);
  }

  [[nodiscard]] bool owns(PoolIndex index) const noexcept {
    return index.value() < N && allocated_[index.value()].load(std::memory_order_relaxed);
  }

  void count_allocation_failure() noexcept {
    allocation_failures_.fetch_add(1U, std::memory_order_relaxed);
  }

  std::array<T, N> values_{};
  // Any releaser may update a slot while a losing allocator still examines a
  // stale head; atomic links make that harmless, while the tagged CAS decides
  // which observation may take ownership.
  std::array<std::atomic<std::uint32_t>, N> next_{};
  std::array<std::atomic_bool, N> allocated_{};
  std::atomic<std::uint64_t> head_{pack(sentinel(), 0U)};
  std::atomic<std::size_t> available_{N};
  std::atomic<std::uint64_t> allocation_failures_{0};
  std::atomic<std::uint64_t> release_failures_{0};
  mutable std::atomic<std::uint64_t> access_failures_{0};
};

} // namespace volt::memory
