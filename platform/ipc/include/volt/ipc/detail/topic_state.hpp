#pragma once

#include "volt/core/error.hpp"
#include "volt/ipc/detail/free_list.hpp"
#include "volt/ipc/detail/index_ring.hpp"
#include "volt/ipc/topic_config.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace volt::ipc::detail {

struct TopicLayout;

/// Bumped whenever the segment layout changes shape. A reader from another
/// build refuses a segment whose version differs instead of misreading it.
inline constexpr std::uint64_t kSegmentVersion = 2;

/// First bytes of a valid, fully initialised segment ("VOLTIPC" + version
/// nibble). Published last, so an opener that sees it sees everything.
inline constexpr std::uint64_t kSegmentMagic = 0x564F'4C54'4950'4331ULL;

/// Marks a port while one process reclaims what its dead owner left behind.
/// Positive values are process identifiers, zero is free.
inline constexpr std::int32_t kPortRecovering = -1;

/// Cache line of every target in SPEC 3.1. Fields written by different
/// parties sit on different lines so ownership traffic does not bounce one
/// line between two cores.
inline constexpr std::size_t kCacheLineBytes = 64;

/// An empty take stage. Any real stage carries a slot index in its high
/// half, and no valid index is all-ones.
inline constexpr std::uint64_t kStageEmpty = 0xFFFF'FFFF'FFFF'FFFFULL;

/// What a slot is currently used for. A stale index cannot silently corrupt
/// the pool: every transition checks the state it came from.
inline constexpr std::uint32_t kSlotFree = 0;
inline constexpr std::uint32_t kSlotLoaned = 1;
inline constexpr std::uint32_t kSlotPublished = 2;

/// Fixed head of the segment. Plain fields never change after creation; the
/// magic is atomic because it is the publication gate.
/// How many times a slot is offered back to the free list before the
/// transport gives up on it. Each attempt fails only because someone else
/// succeeded, so reaching the end means losing a thousand races in a row
/// while a thousand other operations completed.
inline constexpr unsigned kReclaimAttempts = 64;

struct SegmentHeader {
  std::atomic<std::uint64_t> magic;
  std::uint64_t fingerprint;
  std::uint32_t payload_bytes;
  std::uint32_t payload_alignment;
  std::uint32_t slot_count;
  std::uint32_t history_depth;
  std::uint32_t max_subscribers;
  std::uint32_t ring_capacity;
  /// Slots that could not be returned to the pool under contention. Zero on
  /// any healthy topic; a growing value is capacity quietly draining away,
  /// which is why it is counted rather than left to be inferred.
  std::atomic<std::uint64_t> lost_slots;
};

/// The one producer seat of a topic.
struct PublisherPort {
  /// Process that owns the seat; zero when free, kPortRecovering during
  /// reclamation. Alone on its line: subscribers poll it on attach.
  alignas(kCacheLineBytes) std::atomic<std::int32_t> owner;
};

/// One consumer seat: identity, loss accounting, the ring cursors and the
/// take-in-progress stage.
struct SubscriberPort {
  alignas(kCacheLineBytes) std::atomic<std::int32_t> owner;
  /// Messages this consumer lost to DROP_OLDEST, written by the producer.
  /// This is the slow-consumer detector SPEC 12.2 demands: loss is counted
  /// and reported, never hidden.
  std::atomic<std::uint64_t> lost;
  /// Cursors on separate lines: the producer advances the head on every
  /// publish while the consumer advances the tail on every take, and sharing
  /// a line would bounce it between the two cores on every message.
  alignas(kCacheLineBytes) std::atomic<std::uint64_t> ring_head;
  alignas(kCacheLineBytes) std::atomic<std::uint64_t> ring_tail;
  /// Where a take in flight parks its claim before it wins the ring entry,
  /// so a process that dies between winning and recording leaves evidence
  /// instead of a leak. Shares the consumer-written line with the tail.
  std::atomic<std::uint64_t> stage;
};

/// A loan as the engine hands it out: the slot and the loan-table seat that
/// tracks it for crash recovery.
struct LoanTicket {
  std::uint32_t index = kNoSlot;
  std::uint32_t seat = kNoSlot;
};

/// A taken sample: the slot and the held-table seat that records this
/// consumer's reference for crash recovery.
struct SampleTicket {
  std::uint32_t index = kNoSlot;
  std::uint32_t seat = kNoSlot;
};

/// The whole transport engine over one mapped segment, without the payload
/// type: everything below the typed API works on bytes and indices, so one
/// compiled implementation serves every topic and the templates stay thin.
///
/// @thread one publisher process; up to `max_subscribers` subscriber
///         processes; any process may run recovery
class TopicState final {
public:
  TopicState() noexcept = default;

  /// Bytes a segment for this shape needs.
  [[nodiscard]] static core::expected<std::size_t>
  required_bytes(const TopicConfig &config, std::size_t payload_bytes,
                 std::size_t payload_alignment) noexcept;

  /// Builds a fresh segment in zeroed `bytes` and publishes it.
  ///
  /// @pre  `bytes` is zero-filled and at least `required_bytes(...)` long
  /// @post the magic is visible last, so a concurrent opener never sees a
  ///       half-built segment
  [[nodiscard]] static core::expected<TopicState> create_in(std::span<std::byte> bytes,
                                                            const TopicConfig &config,
                                                            std::size_t payload_bytes,
                                                            std::size_t payload_alignment) noexcept;

  /// Attaches to a segment someone else built.
  ///
  /// @errors kResourceUnavailable when the magic is absent (not a topic, or
  ///         not finished being built), kConfigInvalidValue when the
  ///         fingerprint disagrees with this process's payload shape
  [[nodiscard]] static core::expected<TopicState> open_in(std::span<std::byte> bytes,
                                                          std::size_t payload_bytes,
                                                          std::size_t payload_alignment) noexcept;

  /// Claims the producer seat.
  /// @errors kResourceBusy when another live process holds it
  [[nodiscard]] core::expected<void> claim_publisher(std::int32_t process) noexcept;

  /// Returns the producer seat, cancelling outstanding loans.
  void release_publisher() noexcept;

  /// Claims a message slot for writing.
  /// @errors kResourceExhausted when every slot is in flight
  [[nodiscard]] core::expected<LoanTicket> loan() noexcept;

  /// Returns an unpublished loan.
  void cancel_loan(const LoanTicket &ticket) noexcept;

  /// Publishes a loaned slot to every attached subscriber.
  ///
  /// Never blocks and never fails: a full ring evicts its oldest entry and
  /// the loss lands in that port's counter (SPEC 12.2 backpressure).
  ///
  /// @post the caller no longer owns the slot
  /// @return how many subscribers received it
  std::uint32_t publish(const LoanTicket &ticket) noexcept;

  /// Claims a consumer seat.
  /// @errors kResourceExhausted when every seat is taken by a live process
  [[nodiscard]] core::expected<std::uint32_t> claim_subscriber(std::int32_t process) noexcept;

  /// Returns a consumer seat, dropping what it still references.
  void release_subscriber(std::uint32_t port) noexcept;

  /// Takes the oldest pending message.
  /// @errors kResourceUnavailable when nothing is pending
  [[nodiscard]] core::expected<SampleTicket> take(std::uint32_t port) noexcept;

  /// Releases a taken sample; the last reference frees the slot.
  void release_sample(std::uint32_t port, const SampleTicket &ticket) noexcept;

  /// Messages `port` lost to DROP_OLDEST since creation.
  [[nodiscard]] std::uint64_t dropped(std::uint32_t port) const noexcept;

  /// Messages waiting for `port`.
  [[nodiscard]] std::uint64_t pending(std::uint32_t port) const noexcept;

  /// Live consumer seats.
  [[nodiscard]] std::uint32_t subscriber_count() const noexcept;

  /// Free message slots.
  [[nodiscard]] std::uint64_t available_slots() const noexcept;

  /// Slots lost because the pool stayed contended through every attempt.
  [[nodiscard]] std::uint64_t lost_slots() const noexcept;

  /// Payload bytes of slot `index`.
  [[nodiscard]] std::byte *payload_at(std::uint32_t index) noexcept;
  [[nodiscard]] const std::byte *payload_at(std::uint32_t index) const noexcept;

  [[nodiscard]] const SegmentHeader &header() const noexcept { return *header_; }

  /// Reclaims every seat whose owner `alive` reports gone: loans return to
  /// the pool, pending and held samples drop their references, the seat
  /// frees. Safe to run from any process at any time; two racing recoverers
  /// hand out each resource exactly once through the atomic exchanges.
  ///
  /// A predicate rather than a platform: the logic is deterministic and unit
  /// testable with any notion of liveness, and the caller brings the real
  /// one (`IPlatform::process_alive`).
  template <typename Alive> void recover(Alive &&alive) noexcept {
    // Acquire on each owner imports everything the dead process published
    // before it stopped, so the cleanup walks tables it can trust.
    const std::int32_t producer = publisher_->owner.load(std::memory_order_acquire);
    if (producer > 0 && !alive(producer)) {
      recover_publisher(producer);
    }
    for (std::uint32_t port = 0; port < header_->max_subscribers; ++port) {
      const std::int32_t owner = ports_[port].owner.load(std::memory_order_acquire);
      if (owner > 0 && !alive(owner)) {
        recover_subscriber(port, owner);
      }
    }
  }

private:
  /// Wires every pointer to its table. Shared by create and open so the two
  /// can never disagree about where a table lives.
  void map(std::span<std::byte> bytes, const TopicConfig &config,
           const TopicLayout &layout) noexcept;

  /// Returns a loaned slot to the pool.
  void cancel_loan_slot(std::uint32_t index) noexcept;

  /// Returns the references parked in a seat's ring to the pool.
  void drain_ring(std::uint32_t port) noexcept;

  [[nodiscard]] IndexRing ring(std::uint32_t port) noexcept;
  [[nodiscard]] std::span<std::atomic<std::uint32_t>> held(std::uint32_t port) noexcept;
  [[nodiscard]] FreeList free_list() noexcept;

  /// Drops one reference; the last one returns the slot to the pool.
  void drop_reference(std::uint32_t index) noexcept;

  /// Offers a slot back to the free list, retrying while it is contended.
  void return_to_pool(std::uint32_t index) noexcept;

  /// Reclaims the producer seat from `dead`, exactly once across racers.
  void recover_publisher(std::int32_t dead) noexcept;

  /// Reclaims consumer seat `port` from `dead`, exactly once across racers.
  void recover_subscriber(std::uint32_t port, std::int32_t dead) noexcept;

  SegmentHeader *header_ = nullptr;
  std::atomic<std::uint64_t> *free_head_ = nullptr;
  std::atomic<std::uint64_t> *free_available_ = nullptr;
  std::span<std::atomic<std::uint32_t>> next_;
  std::span<std::atomic<std::uint32_t>> state_;
  std::span<std::atomic<std::uint32_t>> refcount_;
  PublisherPort *publisher_ = nullptr;
  std::span<std::atomic<std::uint32_t>> loans_;
  std::span<SubscriberPort> ports_;
  std::span<std::atomic<std::uint32_t>> cells_;
  std::span<std::atomic<std::uint32_t>> held_all_;
  std::byte *payload_ = nullptr;
  std::size_t payload_stride_ = 0;
};

} // namespace volt::ipc::detail
