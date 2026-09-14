#include "volt/ipc/detail/topic_state.hpp"

#include "volt/core/span_utils.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <vector>

namespace volt::ipc::detail {
namespace {

constexpr std::size_t kPayloadBytes = 8;
constexpr std::size_t kPayloadAlignment = 8;
constexpr std::int32_t kProducer = 100;
constexpr std::int32_t kConsumer = 200;

/// A zeroed, cache-line-aligned stand-in for a shared mapping. The engine
/// never asks where its bytes came from, which is exactly what these tests
/// exploit: every scenario runs on a plain buffer, deterministically.
class Segment {
public:
  Segment(const TopicConfig &config, std::size_t bytes) : storage_(bytes + 64U) {
    void *address = storage_.data();
    std::size_t space = storage_.size();
    address = std::align(64U, bytes, address, space);
    span_ = {static_cast<std::byte *>(address), bytes};
    const core::expected<TopicState> state =
        TopicState::create_in(span_, config, kPayloadBytes, kPayloadAlignment);
    EXPECT_TRUE(state.has_value());
    state_ = *state;
  }

  [[nodiscard]] TopicState &state() { return state_; }
  [[nodiscard]] std::span<std::byte> bytes() { return span_; }

private:
  std::vector<std::byte> storage_;
  std::span<std::byte> span_;
  TopicState state_;
};

[[nodiscard]] Segment make_segment(const TopicConfig &config) {
  const core::expected<std::size_t> bytes =
      TopicState::required_bytes(config, kPayloadBytes, kPayloadAlignment);
  EXPECT_TRUE(bytes.has_value());
  return Segment{config, *bytes};
}

void write_payload(TopicState &state, std::uint32_t index, std::uint64_t value) {
  const core::expected<void> written = core::write_little_endian<std::uint64_t>(
      std::span<std::byte>{state.payload_at(index), kPayloadBytes}, 0, value);
  ASSERT_TRUE(written.has_value());
}

[[nodiscard]] std::uint64_t read_payload(TopicState &state, std::uint32_t index) {
  const core::expected<std::uint64_t> value = core::read_little_endian<std::uint64_t>(
      std::span<const std::byte>{state.payload_at(index), kPayloadBytes}, 0);
  EXPECT_TRUE(value.has_value());
  return value.value_or(0);
}

/// Everyone lives unless the test says otherwise.
[[nodiscard]] auto nobody_died() {
  return [](std::int32_t) { return true; };
}

TEST(TopicStateTest, RoundTripsOneMessage) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 2});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> port = state.claim_subscriber(kConsumer);
  ASSERT_TRUE(port.has_value());

  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  write_payload(state, loan->index, 0xC0FFEE);
  EXPECT_EQ(state.publish(*loan), 1U);

  const core::expected<SampleTicket> sample = state.take(*port);
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(read_payload(state, sample->index), 0xC0FFEEU);
  state.release_sample(*port, *sample);
  EXPECT_EQ(state.available_slots(), 4U);
}

TEST(TopicStateTest, KeepsExactlyTheLastNForALaggard) {
  Segment segment = make_segment({.slot_count = 8, .history_depth = 3, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> port = state.claim_subscriber(kConsumer);
  ASSERT_TRUE(port.has_value());

  for (std::uint64_t message = 1; message <= 7; ++message) {
    const core::expected<LoanTicket> loan = state.loan();
    ASSERT_TRUE(loan.has_value());
    write_payload(state, loan->index, message);
    state.publish(*loan);
  }

  // KEEP_LAST(3) + DROP_OLDEST: the consumer that slept through seven
  // messages reads exactly 5, 6, 7 and the four losses are on its counter.
  EXPECT_EQ(state.dropped(*port), 4U);
  for (std::uint64_t expected = 5; expected <= 7; ++expected) {
    const core::expected<SampleTicket> sample = state.take(*port);
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(read_payload(state, sample->index), expected);
    state.release_sample(*port, *sample);
  }
  EXPECT_FALSE(state.take(*port).has_value());
  EXPECT_EQ(state.available_slots(), 8U);
}

TEST(TopicStateTest, SlotSurvivesUntilTheLastReferenceDrops) {
  Segment segment = make_segment({.slot_count = 2, .history_depth = 1, .max_subscribers = 2});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> first = state.claim_subscriber(kConsumer);
  const core::expected<std::uint32_t> second = state.claim_subscriber(kConsumer + 1);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  EXPECT_EQ(state.publish(*loan), 2U);

  const core::expected<SampleTicket> held_by_first = state.take(*first);
  const core::expected<SampleTicket> held_by_second = state.take(*second);
  ASSERT_TRUE(held_by_first.has_value());
  ASSERT_TRUE(held_by_second.has_value());
  EXPECT_EQ(held_by_first->index, held_by_second->index) << "zero copy means one slot";

  state.release_sample(*first, *held_by_first);
  EXPECT_EQ(state.available_slots(), 1U) << "one reader still holds the slot";
  state.release_sample(*second, *held_by_second);
  EXPECT_EQ(state.available_slots(), 2U);
}

TEST(TopicStateTest, CancelledLoanReturnsItsSlot) {
  Segment segment = make_segment({.slot_count = 1, .history_depth = 1, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());

  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  EXPECT_FALSE(state.loan().has_value()) << "the only slot is on loan";
  state.cancel_loan(*loan);
  EXPECT_TRUE(state.loan().has_value());
}

TEST(TopicStateTest, PublishingWithNoSubscribersRecyclesImmediately) {
  Segment segment = make_segment({.slot_count = 2, .history_depth = 1, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());

  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  EXPECT_EQ(state.publish(*loan), 0U);
  EXPECT_EQ(state.available_slots(), 2U);
}

TEST(TopicStateTest, ProducerSeatIsExclusive) {
  Segment segment = make_segment({.slot_count = 2, .history_depth = 1, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<void> second = state.claim_publisher(kProducer + 1);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), core::ErrorCode::kResourceBusy);

  state.release_publisher();
  EXPECT_TRUE(state.claim_publisher(kProducer + 1).has_value());
}

TEST(TopicStateTest, ConsumerSeatsExhaust) {
  Segment segment = make_segment({.slot_count = 2, .history_depth = 1, .max_subscribers = 2});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_subscriber(kConsumer).has_value());
  ASSERT_TRUE(state.claim_subscriber(kConsumer + 1).has_value());
  const core::expected<std::uint32_t> third = state.claim_subscriber(kConsumer + 2);
  ASSERT_FALSE(third.has_value());
  EXPECT_EQ(third.error(), core::ErrorCode::kResourceExhausted);
}

TEST(TopicStateTest, PoolExhaustionSurfacesAtLoan) {
  Segment segment = make_segment({.slot_count = 2, .history_depth = 2, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> port = state.claim_subscriber(kConsumer);
  ASSERT_TRUE(port.has_value());

  // Both slots end up pending in the subscriber ring; the pool is empty and
  // the publisher feels it at loan(), which is the backpressure contract.
  for (int message = 0; message < 2; ++message) {
    const core::expected<LoanTicket> loan = state.loan();
    ASSERT_TRUE(loan.has_value());
    state.publish(*loan);
  }
  const core::expected<LoanTicket> starved = state.loan();
  ASSERT_FALSE(starved.has_value());
  EXPECT_EQ(starved.error(), core::ErrorCode::kResourceExhausted);
}

TEST(TopicStateTest, OpenSeesWhatCreateBuilt) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 2});
  const core::expected<TopicState> opened =
      TopicState::open_in(segment.bytes(), kPayloadBytes, kPayloadAlignment);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->header().slot_count, 4U);
  EXPECT_EQ(opened->available_slots(), 4U);
}

TEST(TopicStateTest, OpenRefusesAForeignPayloadShape) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 2});
  const core::expected<TopicState> opened =
      TopicState::open_in(segment.bytes(), kPayloadBytes * 2, kPayloadAlignment);
  ASSERT_FALSE(opened.has_value());
  EXPECT_EQ(opened.error(), core::ErrorCode::kConfigInvalidValue);
}

TEST(TopicStateTest, OpenRefusesBytesThatAreNoTopic) {
  std::vector<std::byte> zeroes(1024);
  const core::expected<TopicState> opened =
      TopicState::open_in(std::span{zeroes}, kPayloadBytes, kPayloadAlignment);
  ASSERT_FALSE(opened.has_value());
  EXPECT_EQ(opened.error(), core::ErrorCode::kResourceUnavailable);
}

TEST(TopicStateTest, RecoveryReclaimsADeadPublishersLoans) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  ASSERT_TRUE(state.loan().has_value());
  ASSERT_TRUE(state.loan().has_value());
  EXPECT_EQ(state.available_slots(), 2U);

  // The publisher dies holding two loans. Recovery returns the slots and
  // frees the seat for a successor.
  state.recover([](std::int32_t process) { return process != kProducer; });
  EXPECT_EQ(state.available_slots(), 4U);
  EXPECT_TRUE(state.claim_publisher(kProducer + 1).has_value());
}

TEST(TopicStateTest, RecoveryReclaimsADeadConsumersReferences) {
  Segment segment = make_segment({.slot_count = 8, .history_depth = 4, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> port = state.claim_subscriber(kConsumer);
  ASSERT_TRUE(port.has_value());

  // Three messages pending in the ring, two more taken and held: five slots
  // are tied to the consumer when it dies.
  std::vector<SampleTicket> held;
  for (std::uint64_t message = 0; message < 5; ++message) {
    const core::expected<LoanTicket> loan = state.loan();
    ASSERT_TRUE(loan.has_value());
    state.publish(*loan);
    if (message < 2) {
      const core::expected<SampleTicket> sample = state.take(*port);
      ASSERT_TRUE(sample.has_value());
      held.push_back(*sample);
    }
  }
  EXPECT_EQ(state.available_slots(), 3U);

  state.recover([](std::int32_t process) { return process != kConsumer; });
  EXPECT_EQ(state.available_slots(), 8U) << "every reference of the dead consumer returned";
  EXPECT_EQ(state.subscriber_count(), 0U);
  EXPECT_TRUE(state.claim_subscriber(kConsumer + 1).has_value());
}

TEST(TopicStateTest, RecoveryLeavesTheLivingAlone) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> port = state.claim_subscriber(kConsumer);
  ASSERT_TRUE(port.has_value());
  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  write_payload(state, loan->index, 42);
  state.publish(*loan);

  state.recover(nobody_died());

  const core::expected<SampleTicket> sample = state.take(*port);
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(read_payload(state, sample->index), 42U);
  state.release_sample(*port, *sample);
}

TEST(TopicStateTest, RecoveryRunsIdempotently) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 1});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  ASSERT_TRUE(state.loan().has_value());

  const auto producer_died = [](std::int32_t process) { return process != kProducer; };
  state.recover(producer_died);
  state.recover(producer_died);
  EXPECT_EQ(state.available_slots(), 4U) << "a second sweep must find nothing to free";
}

TEST(TopicStateTest, SharedSlotOutlivesItsDeadReader) {
  Segment segment = make_segment({.slot_count = 4, .history_depth = 2, .max_subscribers = 2});
  TopicState &state = segment.state();
  ASSERT_TRUE(state.claim_publisher(kProducer).has_value());
  const core::expected<std::uint32_t> doomed = state.claim_subscriber(kConsumer);
  const core::expected<std::uint32_t> survivor = state.claim_subscriber(kConsumer + 1);
  ASSERT_TRUE(doomed.has_value());
  ASSERT_TRUE(survivor.has_value());

  const core::expected<LoanTicket> loan = state.loan();
  ASSERT_TRUE(loan.has_value());
  write_payload(state, loan->index, 7);
  state.publish(*loan);
  const core::expected<SampleTicket> taken = state.take(*doomed);
  ASSERT_TRUE(taken.has_value());

  // The doomed consumer dies holding the sample; the survivor's pending
  // reference must keep the payload readable.
  state.recover([](std::int32_t process) { return process != kConsumer; });
  const core::expected<SampleTicket> still_there = state.take(*survivor);
  ASSERT_TRUE(still_there.has_value());
  EXPECT_EQ(read_payload(state, still_there->index), 7U);
  state.release_sample(*survivor, *still_there);
  EXPECT_EQ(state.available_slots(), 4U);
}

} // namespace
} // namespace volt::ipc::detail
