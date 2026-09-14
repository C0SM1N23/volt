#pragma once

#include "volt/core/error.hpp"

#include <atomic>
#include <cstdint>
#include <span>

namespace volt::ipc::detail {

/// Index space used across processes; the value equal to the capacity is the
/// empty sentinel, and one extra value marks unused table entries.
inline constexpr std::uint32_t kNoSlot = 0xFFFF'FFFFU;

/// A lock-free stack of free slot indices whose storage belongs to someone
/// else, so it can live in a shared segment mapped at different addresses.
///
/// `platform/memory` already has a fixed pool, but that one owns its arrays
/// and sizes them at compile time; a topic's segment is sized by runtime
/// configuration and its tables are views into shared memory, which is the
/// whole reason this variant exists rather than a duplicate.
///
/// The tag in the packed head prevents ABA when a slot is released and
/// re-allocated between another process's head load and compare-exchange.
/// Collisions retry a bounded number of times: a transport call must not
/// surface a neighbour's compare-exchange as a caller-visible failure, and
/// the bound keeps the loop finite as AGENTS.md 5 requires.
///
/// @thread any process, any thread
/// @rt     allocation-free, lock-free, bounded retries
class FreeList final {
public:
  /// Views an initialised free list.
  ///
  /// @pre every span outlives this view; `next.size() == capacity`
  FreeList(std::atomic<std::uint64_t> &head, std::atomic<std::uint64_t> &available,
           std::span<std::atomic<std::uint32_t>> next) noexcept
      : head_{&head}, available_{&available}, next_{next} {}

  /// Links every slot into the list, newest first. Only the segment creator
  /// calls this, before the segment is published to anyone else.
  void initialise() noexcept;

  /// Claims one slot.
  /// @errors kResourceExhausted when every slot is owned,
  ///         kResourceBusy when the retry bound is exhausted under contention
  [[nodiscard]] core::expected<std::uint32_t> allocate() noexcept;

  /// Returns a slot to the list.
  /// @pre    `index` was claimed from this list and not yet released
  /// @errors kInternalOutOfRange for a foreign index,
  ///         kResourceBusy when the retry bound is exhausted under contention
  [[nodiscard]] core::expected<void> release(std::uint32_t index) noexcept;

  /// Returns a concurrent snapshot of how many slots are free.
  [[nodiscard]] std::uint64_t available() const noexcept {
    return available_->load(std::memory_order_relaxed);
  }

private:
  [[nodiscard]] std::uint32_t capacity() const noexcept {
    return static_cast<std::uint32_t>(next_.size());
  }

  std::atomic<std::uint64_t> *head_;
  std::atomic<std::uint64_t> *available_;
  std::span<std::atomic<std::uint32_t>> next_;
};

} // namespace volt::ipc::detail
