#pragma once

#include "volt/core/error.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

namespace volt::ipc::detail {

/// What a push did besides enqueue: the index it evicted, if the ring was at
/// its history depth.
struct PushOutcome {
  std::optional<std::uint32_t> evicted;
};

/// A single-producer single-consumer ring of slot indices that overwrites its
/// oldest entry instead of refusing a push.
///
/// This is the transport form of KEEP_LAST(n) + DROP_OLDEST from SPEC 12.2:
/// the producer never waits and never fails, a consumer that lags loses the
/// oldest entries, and every loss is handed back to the producer so it can be
/// counted and the evicted message's reference dropped. iceoryx ships the
/// same construct as its used-chunk FiFo, for the same reason.
///
/// The cursors are monotonic 64-bit counts, so they never wrap in practice
/// and equality never suffers ABA. The tail is written by the consumer and,
/// only when evicting, by the producer; that one crossing is what the
/// compare-exchanges below are for, and why this is not `memory::BoundedQueue`.
///
/// @thread one producer calls `push`; one consumer calls `pop`; storage is
///         shared memory, so the two may be different processes
/// @rt     allocation-free, lock-free, bounded retries
class IndexRing final {
public:
  /// Views a ring over external storage.
  ///
  /// @pre `cells.size()` is a power of two, at least `depth`; spans and
  ///      atomics outlive the view
  IndexRing(std::atomic<std::uint64_t> &head, std::atomic<std::uint64_t> &tail,
            std::span<std::atomic<std::uint32_t>> cells, std::uint32_t depth) noexcept
      : head_{&head}, tail_{&tail}, cells_{cells}, depth_{depth} {}

  /// Enqueues `value`, evicting the oldest entry when `depth` are pending.
  ///
  /// A successful eviction also clears a matching intent in `stage`: the
  /// consumer that lost the entry to this eviction may have died before
  /// withdrawing its claim, and a stale claim would later read as ownership.
  ///
  /// @post   on success the consumer eventually observes `value`
  /// @errors kResourceBusy when the eviction retry bound is exhausted, which
  ///         means the consumer kept winning the race; nothing was enqueued
  [[nodiscard]] core::expected<PushOutcome> push(std::uint32_t value,
                                                 std::atomic<std::uint64_t> &stage) noexcept;

  /// Takes the oldest entry, leaving a trail for crash recovery.
  ///
  /// Before competing for the entry the consumer publishes its intent in
  /// `stage`; after securing it the caller records the entry wherever it
  /// lives on and only then clears the stage. A crash at any instant leaves
  /// either the ring entry, the stage, or the caller's record - never
  /// nothing - which is what lets recovery find every reference.
  ///
  /// @post   on success the stage still holds the claim; the caller clears
  ///         it after recording the entry
  /// @errors kResourceUnavailable when the ring is empty,
  ///         kResourceBusy when the retry bound is exhausted under eviction
  [[nodiscard]] core::expected<std::uint32_t> pop(std::atomic<std::uint64_t> &stage) noexcept;

  /// A staged claim keeps the entry in its high half and the low bits of
  /// the ring position in the rest; the split is what lets an eviction
  /// recognise and erase precisely the claim it defeated.
  static constexpr unsigned kClaimValueShift = 32;
  static constexpr std::uint64_t kClaimPositionMask = 0xFFFF'FFFFULL;

  /// Packs a claim: which entry, at which ring position.
  [[nodiscard]] static constexpr std::uint64_t claim(std::uint32_t value,
                                                     std::uint64_t position) noexcept {
    return (static_cast<std::uint64_t>(value) << kClaimValueShift) |
           (position & kClaimPositionMask);
  }

  /// Reads the entry a claim names.
  [[nodiscard]] static constexpr std::uint32_t claimed_value(std::uint64_t stage_word) noexcept {
    return static_cast<std::uint32_t>(stage_word >> kClaimValueShift);
  }

  /// Returns a concurrent snapshot of how many entries wait.
  [[nodiscard]] std::uint64_t pending() const noexcept;

private:
  [[nodiscard]] std::uint64_t mask() const noexcept { return cells_.size() - 1U; }

  std::atomic<std::uint64_t> *head_;
  std::atomic<std::uint64_t> *tail_;
  std::span<std::atomic<std::uint32_t>> cells_;
  std::uint32_t depth_;
};

} // namespace volt::ipc::detail
