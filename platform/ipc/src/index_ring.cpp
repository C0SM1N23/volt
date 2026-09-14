#include "volt/ipc/detail/index_ring.hpp"

#include "volt/ipc/detail/topic_state.hpp"

namespace volt::ipc::detail {
namespace {

/// Every failed attempt means the other side advanced the tail, so the loop
/// cannot spin without global progress; the bound only caps the local cost.
constexpr unsigned kRetryBound = 128;

} // namespace

core::expected<PushOutcome> IndexRing::push(std::uint32_t value,
                                            std::atomic<std::uint64_t> &stage) noexcept {
  // Relaxed: the producer is the only writer of the head cursor.
  const std::uint64_t head = head_->load(std::memory_order_relaxed);
  PushOutcome outcome{};

  VOLT_LOOP_BOUND(kRetryBound);
  for (unsigned attempt = 0; attempt < kRetryBound; ++attempt) {
    // Acquire pairs with the consumer's tail publication, so the cell the
    // producer is about to overwrite has been fully read out.
    const std::uint64_t tail = tail_->load(std::memory_order_acquire);
    if (head - tail < depth_) {
      // Relaxed: the head store below releases the cell to the consumer.
      cells_[head & mask()].store(value, std::memory_order_relaxed);
      // Release publishes the cell, and everything the caller wrote into the
      // message it names, to the consumer's acquire of the head.
      head_->store(head + 1U, std::memory_order_release);
      return outcome;
    }

    // Full: claim the oldest entry instead of waiting for the consumer,
    // which is the DROP_OLDEST contract. Relaxed cell read: the producer
    // wrote this cell itself, and ownership is decided by the exchange below.
    const std::uint32_t victim = cells_[tail & mask()].load(std::memory_order_relaxed);
    std::uint64_t expected = tail;
    // Acquire-release: success transfers the entry to the producer the same
    // way a consumer pop would take it; failure means the consumer took it,
    // and the refreshed tail arrives through the next acquire load.
    if (tail_->compare_exchange_strong(expected, tail + 1U, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
      outcome.evicted = victim;
      // The consumer may have staked this very entry and lost; if it then
      // died before withdrawing, the stale claim would read as ownership at
      // recovery and the entry would be freed twice. The winner erases the
      // loser's intent. Acquire-release: settles against the consumer's own
      // concurrent withdrawal, either order leaving the stage empty.
      std::uint64_t stale = claim(victim, tail);
      static_cast<void>(stage.compare_exchange_strong(stale, kStageEmpty, std::memory_order_acq_rel,
                                                      std::memory_order_relaxed));
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

core::expected<std::uint32_t> IndexRing::pop(std::atomic<std::uint64_t> &stage) noexcept {
  VOLT_LOOP_BOUND(kRetryBound);
  for (unsigned attempt = 0; attempt < kRetryBound; ++attempt) {
    // Relaxed first read: the value is only trusted after the exchange.
    const std::uint64_t tail = tail_->load(std::memory_order_relaxed);
    // Acquire pairs with the producer's head release, importing the cell
    // and the message payload it names.
    const std::uint64_t head = head_->load(std::memory_order_acquire);
    if (tail == head) {
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }
    // Atomic cell: when the producer loses an eviction race it may overwrite
    // this very cell for the next lap while this load runs; the exchange
    // below then fails and the value is discarded, but the read itself must
    // not be a data race.
    const std::uint32_t value = cells_[tail & mask()].load(std::memory_order_relaxed);

    // Intent goes on record before the race: from here to the moment the
    // caller files the entry somewhere durable, a crash leaves this claim
    // for recovery to judge. Release publishes it before the exchange can
    // make it true.
    stage.store(claim(value, tail), std::memory_order_release);

    std::uint64_t expected = tail;
    // Acquire-release: success takes exclusive ownership of the entry
    // against a producer eviction of the same one.
    if (tail_->compare_exchange_strong(expected, tail + 1U, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
      return value;
    }
    // Lost to an eviction: withdraw the claim. The producer clears a stale
    // matching claim on its side too, so a crash exactly here stays benign.
    stage.store(kStageEmpty, std::memory_order_release);
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

std::uint64_t IndexRing::pending() const noexcept {
  // Both relaxed: a snapshot for diagnostics, precise only in quiescence.
  const std::uint64_t tail = tail_->load(std::memory_order_relaxed);
  const std::uint64_t head = head_->load(std::memory_order_relaxed);
  return head - tail;
}

} // namespace volt::ipc::detail
