#include "volt/ipc/detail/free_list.hpp"

namespace volt::ipc::detail {
namespace {

/// Retries before a call reports contention.
///
/// Every failed attempt means some other party succeeded, so the structure as
/// a whole never stalls - only this caller does. The bound exists to keep the
/// loop finite (AGENTS.md 5.7), not because exhausting it is expected: a
/// slower machine with more contenders reaches a hundred consecutive losses
/// often enough to matter, and a thousand essentially never.
constexpr unsigned kRetryBound = 1024;

constexpr unsigned kTagShiftBits = 32;
constexpr std::uint64_t kIndexMask = 0xFFFF'FFFFULL;

[[nodiscard]] constexpr std::uint64_t pack(std::uint32_t index, std::uint32_t tag) noexcept {
  return (static_cast<std::uint64_t>(tag) << kTagShiftBits) | index;
}
[[nodiscard]] constexpr std::uint32_t index_of(std::uint64_t head) noexcept {
  return static_cast<std::uint32_t>(head & kIndexMask);
}
[[nodiscard]] constexpr std::uint32_t tag_of(std::uint64_t head) noexcept {
  return static_cast<std::uint32_t>(head >> kTagShiftBits);
}

} // namespace

void FreeList::initialise() noexcept {
  for (std::uint32_t index = 0; index < capacity(); ++index) {
    // Relaxed: the segment header's release publication makes every store
    // here visible before any other process can observe the segment.
    next_[index].store(index + 1U, std::memory_order_relaxed);
  }
  head_->store(pack(0U, 0U), std::memory_order_relaxed);
  available_->store(capacity(), std::memory_order_relaxed);
}

core::expected<std::uint32_t> FreeList::allocate() noexcept {
  VOLT_LOOP_BOUND(kRetryBound);
  for (unsigned attempt = 0; attempt < kRetryBound; ++attempt) {
    std::uint64_t observed = head_->load(std::memory_order_acquire);
    const std::uint32_t index = index_of(observed);
    if (index == capacity()) {
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }
    // Relaxed: the acquire head load already imported the link published by
    // the release that made this index reachable.
    const std::uint32_t next = next_[index].load(std::memory_order_relaxed);
    // Acquire-release: the success imports the previous owner's writes and
    // publishes the new head to competing allocators; the failure keeps the
    // refreshed head usable on the next attempt.
    if (head_->compare_exchange_strong(observed, pack(next, tag_of(observed) + 1U),
                                       std::memory_order_acq_rel, std::memory_order_acquire)) {
      // Relaxed: a diagnostic gauge, ordered by nothing.
      available_->fetch_sub(1U, std::memory_order_relaxed);
      return index;
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

core::expected<void> FreeList::release(std::uint32_t index) noexcept {
  if (index >= capacity()) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  VOLT_LOOP_BOUND(kRetryBound);
  for (unsigned attempt = 0; attempt < kRetryBound; ++attempt) {
    std::uint64_t observed = head_->load(std::memory_order_relaxed);
    // Relaxed: the release compare-exchange below publishes this link
    // together with the new head.
    next_[index].store(index_of(observed), std::memory_order_relaxed);
    // Release publishes the link and the slot's final state to the next
    // allocator; a failed attempt still owns the slot, so relaxed reread.
    if (head_->compare_exchange_strong(observed, pack(index, tag_of(observed) + 1U),
                                       std::memory_order_release, std::memory_order_relaxed)) {
      available_->fetch_add(1U, std::memory_order_relaxed);
      return {};
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

} // namespace volt::ipc::detail
