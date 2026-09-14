#include "volt/ipc/detail/topic_state.hpp"

#include "volt/ipc/detail/topic_layout.hpp"

#include <optional>

namespace volt::ipc::detail {
namespace {

/// Casts a table offset into a typed view. The segment is bytes to C++, but
/// every process writes the same object shapes at the same offsets, and every
/// shared word is a lock-free atomic; this cast is the one place the type
/// system is told so. Confined here the way sockaddr casts are confined to
/// the POSIX backend (AGENTS.md 2.7).
template <typename T> [[nodiscard]] T *table_at(std::span<std::byte> bytes, std::size_t offset) {
  return reinterpret_cast<T *>(bytes.data() + offset);
}

/// Bounded seat search shared by the loan and held tables.
[[nodiscard]] std::uint32_t claim_seat(std::span<std::atomic<std::uint32_t>> table,
                                       std::uint32_t value) noexcept {
  for (std::uint32_t seat = 0; seat < table.size(); ++seat) {
    std::uint32_t expected = kNoSlot;
    // Acquire-release: taking a seat both claims it against a concurrent
    // recoverer and publishes the recorded index to one.
    if (table[seat].compare_exchange_strong(expected, value, std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
      return seat;
    }
  }
  return kNoSlot;
}

} // namespace

core::expected<std::size_t> TopicState::required_bytes(const TopicConfig &config,
                                                       std::size_t payload_bytes,
                                                       std::size_t payload_alignment) noexcept {
  const core::expected<TopicLayout> layout =
      compute_layout(config, payload_bytes, payload_alignment);
  if (!layout.has_value()) {
    return std::unexpected{layout.error()};
  }
  return layout->total_bytes;
}

core::expected<TopicState> TopicState::create_in(std::span<std::byte> bytes,
                                                 const TopicConfig &config,
                                                 std::size_t payload_bytes,
                                                 std::size_t payload_alignment) noexcept {
  const core::expected<TopicLayout> layout =
      compute_layout(config, payload_bytes, payload_alignment);
  if (!layout.has_value()) {
    return std::unexpected{layout.error()};
  }
  if (bytes.size() < layout->total_bytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  TopicState state;
  state.map(bytes, config, *layout);

  // The mapping is zero-filled by the platform; zero is already the correct
  // initial value for every table (free seats, free slots, zero counters),
  // so only the free list's links and the header need explicit stores.
  state.free_list().initialise();
  for (std::atomic<std::uint32_t> &seat : state.loans_) {
    seat.store(kNoSlot, std::memory_order_relaxed);
  }
  for (std::atomic<std::uint32_t> &seat : state.held_all_) {
    seat.store(kNoSlot, std::memory_order_relaxed);
  }
  for (SubscriberPort &port : state.ports_) {
    port.stage.store(kStageEmpty, std::memory_order_relaxed);
  }

  SegmentHeader &header = *state.header_;
  header.fingerprint = layout_fingerprint(config, payload_bytes, payload_alignment, *layout);
  header.payload_bytes = static_cast<std::uint32_t>(payload_bytes);
  header.payload_alignment = static_cast<std::uint32_t>(payload_alignment);
  header.slot_count = config.slot_count;
  header.history_depth = config.history_depth;
  header.max_subscribers = config.max_subscribers;
  header.ring_capacity = layout->ring_capacity;
  // Release publishes every store above to the opener's acquire of the
  // magic: whoever sees a valid magic sees a complete segment.
  header.magic.store(kSegmentMagic, std::memory_order_release);
  return state;
}

core::expected<TopicState> TopicState::open_in(std::span<std::byte> bytes,
                                               std::size_t payload_bytes,
                                               std::size_t payload_alignment) noexcept {
  if (bytes.size() < sizeof(SegmentHeader)) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  SegmentHeader *header = table_at<SegmentHeader>(bytes, 0);
  // Acquire pairs with the creator's release: a valid magic guarantees the
  // rest of the header and every table behind it.
  if (header->magic.load(std::memory_order_acquire) != kSegmentMagic) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  const TopicConfig config{.slot_count = header->slot_count,
                           .history_depth = header->history_depth,
                           .max_subscribers = header->max_subscribers};
  const core::expected<TopicLayout> layout =
      compute_layout(config, header->payload_bytes, header->payload_alignment);
  if (!layout.has_value()) {
    return std::unexpected{layout.error()};
  }
  // The fingerprint is recomputed from this process's own payload shape, so
  // a topic opened with the wrong type, or a layout from another build, is
  // refused instead of misread.
  const std::uint64_t expected =
      layout_fingerprint(config, payload_bytes, payload_alignment, *layout);
  if (header->fingerprint != expected || bytes.size() < layout->total_bytes) {
    return std::unexpected{core::ErrorCode::kConfigInvalidValue};
  }

  TopicState state;
  state.map(bytes, config, *layout);
  return state;
}

void TopicState::map(std::span<std::byte> bytes, const TopicConfig &config,
                     const TopicLayout &layout) noexcept {
  header_ = table_at<SegmentHeader>(bytes, 0);
  free_head_ = table_at<std::atomic<std::uint64_t>>(bytes, layout.free_head);
  free_available_ = table_at<std::atomic<std::uint64_t>>(bytes, layout.free_available);
  next_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.next_table), config.slot_count};
  state_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.state_table), config.slot_count};
  refcount_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.refcount_table),
               config.slot_count};
  publisher_ = table_at<PublisherPort>(bytes, layout.publisher_port);
  loans_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.loan_table), config.slot_count};
  ports_ = {table_at<SubscriberPort>(bytes, layout.subscriber_ports), config.max_subscribers};
  cells_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.ring_cells),
            static_cast<std::size_t>(config.max_subscribers) * layout.ring_capacity};

  held_all_ = {table_at<std::atomic<std::uint32_t>>(bytes, layout.held_tables),
               static_cast<std::size_t>(config.max_subscribers) * config.slot_count};
  payload_ = bytes.data() + layout.payload;
  payload_stride_ = layout.payload_stride;
}

IndexRing TopicState::ring(std::uint32_t port) noexcept {
  const std::size_t capacity = header_->ring_capacity;
  return IndexRing{ports_[port].ring_head, ports_[port].ring_tail,
                   cells_.subspan(port * capacity, capacity), header_->history_depth};
}

std::span<std::atomic<std::uint32_t>> TopicState::held(std::uint32_t port) noexcept {
  return held_all_.subspan(static_cast<std::size_t>(port) * header_->slot_count,
                           header_->slot_count);
}

FreeList TopicState::free_list() noexcept { return FreeList{*free_head_, *free_available_, next_}; }

core::expected<void> TopicState::claim_publisher(std::int32_t process) noexcept {
  std::int32_t expected = 0;
  // Acquire-release: success imports the previous owner's released state and
  // publishes this claim to every other process.
  if (!publisher_->owner.compare_exchange_strong(expected, process, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  return {};
}

void TopicState::release_publisher() noexcept {
  for (std::atomic<std::uint32_t> &seat : loans_) {
    // Acquire-release: whoever wins the exchange, this releaser or a racing
    // recoverer, is the one that returns the slot.
    const std::uint32_t index = seat.exchange(kNoSlot, std::memory_order_acq_rel);
    if (index != kNoSlot) {
      cancel_loan_slot(index);
    }
  }
  // Release publishes the cancelled loans before the seat looks free.
  publisher_->owner.store(0, std::memory_order_release);
}

core::expected<LoanTicket> TopicState::loan() noexcept {
  const core::expected<std::uint32_t> index = free_list().allocate();
  if (!index.has_value()) {
    return std::unexpected{index.error()};
  }

  std::uint32_t expected = kSlotFree;
  const bool transitioned = state_[*index].compare_exchange_strong(
      // Relaxed: the free list's own ordering already transferred the slot, and
      // the state word is a guard against stale indices, not a publication.
      expected, kSlotLoaned, std::memory_order_relaxed, std::memory_order_relaxed);
  VOLT_ASSERT(transitioned, "a slot left the free list while not free");

  // Relaxed: the publisher is the only referent until publish distributes
  // the slot, and that distribution carries the ordering.
  refcount_[*index].store(1U, std::memory_order_relaxed);

  const std::uint32_t seat = claim_seat(loans_, *index);
  VOLT_ASSERT(seat != kNoSlot, "loan table smaller than the slot pool");
  return LoanTicket{.index = *index, .seat = seat};
}

void TopicState::cancel_loan_slot(std::uint32_t index) noexcept {
  std::uint32_t expected = kSlotLoaned;
  const bool transitioned = state_[index].compare_exchange_strong(
      // Relaxed: see loan(); the free list release below publishes.
      expected, kSlotFree, std::memory_order_relaxed, std::memory_order_relaxed);
  VOLT_ASSERT(transitioned, "cancelling a loan that was not loaned");
  refcount_[index].store(0U, std::memory_order_relaxed);
  return_to_pool(index);
}

void TopicState::cancel_loan(const LoanTicket &ticket) noexcept {
  // Acquire-release: settles the race against a recoverer walking the table.
  const std::uint32_t index = loans_[ticket.seat].exchange(kNoSlot, std::memory_order_acq_rel);
  if (index != kNoSlot) {
    cancel_loan_slot(index);
  }
}

std::uint32_t TopicState::publish(const LoanTicket &ticket) noexcept {
  const std::uint32_t index = ticket.index;
  std::uint32_t expected = kSlotLoaned;
  const bool transitioned = state_[index].compare_exchange_strong(
      // Relaxed: the ring push releases the payload to each consumer.
      expected, kSlotPublished, std::memory_order_relaxed, std::memory_order_relaxed);
  VOLT_ASSERT(transitioned, "publishing a slot that was not loaned");

  std::uint32_t deliveries = 0;
  for (std::uint32_t port = 0; port < header_->max_subscribers; ++port) {
    // Acquire pairs with a releasing subscriber, so a freed seat is skipped
    // together with everything it left behind.
    if (ports_[port].owner.load(std::memory_order_acquire) <= 0) {
      continue;
    }
    // The reference is granted before the entry exists, so no consumer can
    // ever observe an entry whose reference is missing. Relaxed: the ring
    // push carries the ordering.
    refcount_[index].fetch_add(1U, std::memory_order_relaxed);
    const core::expected<PushOutcome> outcome = ring(port).push(index, ports_[port].stage);
    if (!outcome.has_value()) {
      // The eviction retry bound was exhausted; the entry was never
      // enqueued, so the granted reference comes straight back.
      refcount_[index].fetch_sub(1U, std::memory_order_relaxed);
      ports_[port].lost.fetch_add(1U, std::memory_order_relaxed);
      continue;
    }
    deliveries += 1;
    const std::optional<std::uint32_t> evicted = outcome->evicted;
    if (evicted.has_value()) {
      // DROP_OLDEST: the consumer never saw this entry; its reference moves
      // from the ring back to the pool, and the loss is counted, not hidden.
      ports_[port].lost.fetch_add(1U, std::memory_order_relaxed);
      drop_reference(*evicted);
    }
  }

  // Acquire-release: settles the loan-table race against a recoverer.
  static_cast<void>(loans_[ticket.seat].exchange(kNoSlot, std::memory_order_acq_rel));
  // The publisher's own reference from loan(); with no subscribers this is
  // the last one and the slot returns to the pool immediately.
  drop_reference(index);
  return deliveries;
}

core::expected<std::uint32_t> TopicState::claim_subscriber(std::int32_t process) noexcept {
  for (std::uint32_t port = 0; port < header_->max_subscribers; ++port) {
    std::int32_t expected = 0;
    // Acquire-release: success imports the previous tenant's cleanup and
    // publishes this claim to the publisher's fan-out scan.
    if (!ports_[port].owner.compare_exchange_strong(expected, process, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed)) {
      continue;
    }
    // A publisher racing the previous tenant's departure may have parked
    // entries in the ring after that tenant's drain; they are valid
    // messages this seat never asked for, so their references go back.
    drain_ring(port);
    return port;
  }
  return std::unexpected{core::ErrorCode::kResourceExhausted};
}

void TopicState::drain_ring(std::uint32_t port) noexcept {
  IndexRing view = ring(port);
  // The drain stakes its own claims through the seat's stage, exactly like
  // a take: a recoverer that dies mid-drain is itself recoverable.
  std::atomic<std::uint64_t> &stage = ports_[port].stage;
  // Bounded by the ring's own depth: pop only fails spuriously against an
  // evicting producer, and each success shrinks the pending count.
  const std::uint64_t bound = (2ULL * header_->ring_capacity) + 2ULL;
  for (std::uint64_t attempt = 0; attempt < bound; ++attempt) {
    const core::expected<std::uint32_t> index = view.pop(stage);
    if (index.has_value()) {
      drop_reference(*index);
      stage.store(kStageEmpty, std::memory_order_release);
      continue;
    }
    if (index.error() == core::ErrorCode::kResourceUnavailable) {
      return;
    }
  }
}

void TopicState::release_subscriber(std::uint32_t port) noexcept {
  // Order matters: references still recorded in the tables go back first,
  // then the seat frees. A publisher can push into the ring during this
  // window; those entries stay parked, bounded by the ring depth, until the
  // next claim of this seat drains them.
  //
  // The stage is judged before the drain moves the cursors it is compared
  // against. Acquire-release: the winner against the port's own late clear
  // is the one that acts on the claim.
  const std::uint64_t staked = ports_[port].stage.exchange(kStageEmpty, std::memory_order_acq_rel);
  drain_ring(port);
  if (staked != kStageEmpty) {
    // The dead consumer had won a ring entry but not yet filed it - unless
    // it crashed one instruction later, with the entry already in the held
    // table and only the withdrawal missing. The held walk below owns that
    // second case, so the claim counts only when no seat records the entry.
    const std::uint32_t index = IndexRing::claimed_value(staked);
    bool filed = false;
    for (std::atomic<std::uint32_t> &seat : held(port)) {
      // Relaxed: the port owner is dead and the exchange above serialised
      // racing recoverers; nobody mutates these seats concurrently.
      filed = filed || seat.load(std::memory_order_relaxed) == index;
    }
    if (!filed) {
      drop_reference(index);
    }
  }
  for (std::atomic<std::uint32_t> &seat : held(port)) {
    // Acquire-release: the winner against a racing recoverer drops it.
    const std::uint32_t index = seat.exchange(kNoSlot, std::memory_order_acq_rel);
    if (index != kNoSlot) {
      drop_reference(index);
    }
  }
  // Release publishes the cleanup before the seat looks free.
  ports_[port].owner.store(0, std::memory_order_release);
}

core::expected<SampleTicket> TopicState::take(std::uint32_t port) noexcept {
  std::atomic<std::uint64_t> &stage = ports_[port].stage;
  const core::expected<std::uint32_t> index = ring(port).pop(stage);
  if (!index.has_value()) {
    return std::unexpected{index.error()};
  }
  // Between the pop and this record the stage is the reference's only home.
  // The order - file it in the held table first, withdraw the claim second -
  // means a crash anywhere leaves at least one trace, and the recovery
  // dedupe tolerates the instant where both exist.
  const std::uint32_t seat = claim_seat(held(port), *index);
  VOLT_ASSERT(seat != kNoSlot, "held table smaller than the slot pool");
  stage.store(kStageEmpty, std::memory_order_release);
  return SampleTicket{.index = *index, .seat = seat};
}

void TopicState::release_sample(std::uint32_t port, const SampleTicket &ticket) noexcept {
  // Acquire-release: settles the race against a recoverer walking the table.
  const std::uint32_t index = held(port)[ticket.seat].exchange(kNoSlot, std::memory_order_acq_rel);
  if (index != kNoSlot) {
    drop_reference(index);
  }
}

void TopicState::return_to_pool(std::uint32_t index) noexcept {
  VOLT_LOOP_BOUND(kReclaimAttempts);
  for (unsigned attempt = 0; attempt < kReclaimAttempts; ++attempt) {
    const core::expected<void> released = free_list().release(index);
    if (released.has_value()) {
      return;
    }
    if (released.error() != core::ErrorCode::kResourceBusy) {
      // A foreign or already-free index is a defect in this file, not
      // something another thread can cause by being busy.
      VOLT_ASSERT(false, "a slot outside this pool was returned to it");
      return;
    }
  }
  // Losing a buffer costs the topic one slot of capacity, which
  // `available_slots` makes visible and this counts. Aborting instead would
  // cost the vehicle the whole function, over a race that resolves itself
  // everywhere else. Relaxed: a diagnostic that orders nothing.
  header_->lost_slots.fetch_add(1U, std::memory_order_relaxed);
}

void TopicState::drop_reference(std::uint32_t index) noexcept {
  // Acquire-release, the shared-ownership classic: the release half orders
  // this referent's reads before the decrement, the acquire half makes the
  // last decrementer see every other referent's reads before it frees.
  const std::uint32_t previous = refcount_[index].fetch_sub(1U, std::memory_order_acq_rel);
  VOLT_ASSERT(previous != 0U, "a reference was dropped that was never granted");
  if (previous != 1U) {
    return;
  }
  std::uint32_t expected = kSlotPublished;
  const bool transitioned = state_[index].compare_exchange_strong(
      // Relaxed: the free list release publishes the transition.
      expected, kSlotFree, std::memory_order_relaxed, std::memory_order_relaxed);
  VOLT_ASSERT(transitioned, "the last reference found a slot not published");
  return_to_pool(index);
}

std::uint64_t TopicState::dropped(std::uint32_t port) const noexcept {
  // Relaxed: a monotonic counter read for reporting.
  return ports_[port].lost.load(std::memory_order_relaxed);
}

std::uint64_t TopicState::pending(std::uint32_t port) const noexcept {
  // Both relaxed: a snapshot for diagnostics, precise only in quiescence.
  const SubscriberPort &seat = ports_[port];
  return seat.ring_head.load(std::memory_order_relaxed) -
         seat.ring_tail.load(std::memory_order_relaxed);
}

std::uint32_t TopicState::subscriber_count() const noexcept {
  std::uint32_t count = 0;
  for (const SubscriberPort &port : ports_) {
    // Relaxed: a snapshot for reporting; attachment is racy by nature.
    if (port.owner.load(std::memory_order_relaxed) > 0) {
      count += 1;
    }
  }
  return count;
}

std::uint64_t TopicState::lost_slots() const noexcept {
  // Relaxed: a monotonic diagnostic counter read for reporting.
  return header_->lost_slots.load(std::memory_order_relaxed);
}

std::uint64_t TopicState::available_slots() const noexcept {
  // Relaxed: a diagnostic gauge.
  return free_available_->load(std::memory_order_relaxed);
}

std::byte *TopicState::payload_at(std::uint32_t index) noexcept {
  return payload_ + (static_cast<std::size_t>(index) * payload_stride_);
}

const std::byte *TopicState::payload_at(std::uint32_t index) const noexcept {
  return payload_ + (static_cast<std::size_t>(index) * payload_stride_);
}

void TopicState::recover_publisher(std::int32_t dead) noexcept {
  std::int32_t expected = dead;
  // Acquire-release: exactly one recoverer wins the seat and imports what
  // the dead process last published; everyone else sees the marker and
  // leaves. Without this claim a second recoverer could free the seat while
  // the first still walks the loan table, handing it to a new publisher
  // whose loans the first would then cancel.
  if (!publisher_->owner.compare_exchange_strong(
          expected, kPortRecovering, std::memory_order_acq_rel, std::memory_order_relaxed)) {
    return;
  }
  release_publisher();
}

void TopicState::recover_subscriber(std::uint32_t port, std::int32_t dead) noexcept {
  std::int32_t expected = dead;
  // Same exclusive-claim reasoning as the publisher seat.
  if (!ports_[port].owner.compare_exchange_strong(
          expected, kPortRecovering, std::memory_order_acq_rel, std::memory_order_relaxed)) {
    return;
  }
  release_subscriber(port);
}

} // namespace volt::ipc::detail
