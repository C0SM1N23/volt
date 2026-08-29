#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace volt::log {

/// Bytes reserved for one record.
///
/// Fixed-size slots rather than a packed byte stream: a variable-length ring
/// has to handle a record straddling the end, and that handling is the part
/// that goes wrong under concurrency. A slot large enough for a header, eight
/// arguments and a capped text argument costs a little memory and removes the
/// case entirely.
inline constexpr std::size_t kSlotBytes = 256;

/// Slots per thread. A power of two so the index wraps with a mask instead of
/// a division on the caller path.
///
/// At 256 bytes a slot this is a quarter of a megabyte per logging thread,
/// which buys roughly a thousand records of burst before a drain has to run.
inline constexpr std::size_t kSlotsPerRing = 1024;

/// A single-producer, single-consumer queue of records.
///
/// One of these belongs to each producing thread, and the drain thread is the
/// only consumer, so the many-producer topology of SPEC 8.4 is built from
/// queues that never contend. Nothing here blocks: a producer that finds the
/// ring full drops the record and counts it, because stalling a control cycle
/// to make room for a log line is never the right trade.
///
/// @thread `push` from the owning thread only; `pop` from the drain thread only
class LogRing final {
public:
  LogRing();

  // Rule of five because the object is shared between two threads through a
  // registered pointer: moving it would leave the drain reading freed storage.
  ~LogRing() = default;
  LogRing(const LogRing &) = delete;
  LogRing &operator=(const LogRing &) = delete;
  LogRing(LogRing &&) = delete;
  LogRing &operator=(LogRing &&) = delete;

  /// Returns the slot the next record is written into.
  ///
  /// @post the span is valid until `publish` or the next `claim`
  /// @thread the owning thread only
  /// @rt     allocation-free, wait-free
  [[nodiscard]] std::span<std::byte> claim() noexcept;

  /// Makes the claimed slot visible to the drain.
  ///
  /// @pre    `used_bytes` is what the writer reported, and is not zero
  /// @thread the owning thread only
  void publish(std::size_t used_bytes) noexcept;

  /// Records that a message was thrown away.
  ///
  /// @thread the owning thread only
  void drop() noexcept;

  /// Takes the oldest record, or an empty span when there is none.
  ///
  /// @post   the span is valid until the next `pop` on this ring
  /// @thread the drain thread only
  [[nodiscard]] std::span<const std::byte> pop() noexcept;

  /// Returns how many records this ring has thrown away.
  ///
  /// Nothing is ever lost silently: a drop is counted here and reported by the
  /// drain, which is what SPEC 42.1 requires of every error.
  [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
  [[nodiscard]] std::span<std::byte> slot_at(std::size_t index) noexcept;

  std::vector<std::byte> storage_;
  std::vector<std::uint16_t> lengths_;

  // Written only by the producer, read by both. Release on publish pairs with
  // the consumer's acquire, so the bytes written into the slot are visible
  // before the index that exposes them.
  std::atomic<std::size_t> write_index_{0};

  // How many records the drain has finished with, so the producer knows which
  // slots are free. Written only by the consumer, read by both. Release pairs
  // with the producer's acquire in `claim`.
  std::atomic<std::size_t> read_index_{0};

  // Only ever incremented by the producer and read by the drain; no ordering
  // is needed because the count carries no other data with it.
  std::atomic<std::uint64_t> dropped_{0};

  std::size_t claimed_index_ = 0;

  // Consumer-only: the next record to hand out. Kept apart from `read_index_`
  // because a record stays live in the caller's hands after it is taken.
  std::size_t next_to_read_ = 0;
};

} // namespace volt::log
