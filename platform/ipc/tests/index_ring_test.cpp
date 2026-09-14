#include "volt/ipc/detail/index_ring.hpp"

#include "volt/ipc/detail/topic_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <vector>

namespace volt::ipc::detail {
namespace {

constexpr std::uint32_t kDepth = 3;
constexpr std::size_t kCapacity = 4; // next power of two above the depth

class IndexRingTest : public ::testing::Test {
protected:
  IndexRingTest() { stage_.store(kStageEmpty); }

  IndexRing ring() { return IndexRing{head_, tail_, cells_, kDepth}; }
  [[nodiscard]] core::expected<PushOutcome> push(std::uint32_t value) {
    return ring().push(value, stage_);
  }
  [[nodiscard]] core::expected<std::uint32_t> pop() {
    const core::expected<std::uint32_t> value = ring().pop(stage_);
    // What a real caller does after filing the entry; the crash-window
    // behaviour of a stale stage has its own tests below.
    stage_.store(kStageEmpty);
    return value;
  }

  std::atomic<std::uint64_t> head_{};
  std::atomic<std::uint64_t> tail_{};
  std::array<std::atomic<std::uint32_t>, kCapacity> cells_{};
  std::atomic<std::uint64_t> stage_{};
};

TEST_F(IndexRingTest, DeliversInFifoOrder) {
  for (std::uint32_t value = 1; value <= kDepth; ++value) {
    const core::expected<PushOutcome> outcome = push(value);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->evicted.has_value());
  }
  for (std::uint32_t value = 1; value <= kDepth; ++value) {
    const core::expected<std::uint32_t> popped = pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, value);
  }
}

TEST_F(IndexRingTest, PopReportsEmptiness) {
  const core::expected<std::uint32_t> popped = pop();
  ASSERT_FALSE(popped.has_value());
  EXPECT_EQ(popped.error(), core::ErrorCode::kResourceUnavailable);
}

TEST_F(IndexRingTest, EvictsTheOldestWhenFull) {
  for (std::uint32_t value = 1; value <= kDepth; ++value) {
    ASSERT_TRUE(push(value).has_value());
  }
  // The fourth push crosses the KEEP_LAST(3) depth: entry 1 must come back
  // to the producer, and the consumer must now see exactly 2, 3, 4.
  const core::expected<PushOutcome> outcome = push(4);
  ASSERT_TRUE(outcome.has_value());
  const std::optional<std::uint32_t> evicted = outcome->evicted;
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(evicted.value_or(0U), 1U);

  for (std::uint32_t value = 2; value <= 4; ++value) {
    const core::expected<std::uint32_t> popped = pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, value);
  }
  EXPECT_FALSE(pop().has_value());
}

TEST_F(IndexRingTest, HoldsExactlyTheHistoryDepth) {
  // The capacity is rounded up to a power of two, but the QoS depth is what
  // the caller asked for; the ring must evict at the depth, not the capacity.
  for (std::uint32_t value = 1; value <= kDepth; ++value) {
    ASSERT_TRUE(push(value).has_value());
  }
  EXPECT_EQ(ring().pending(), kDepth);
  const core::expected<PushOutcome> outcome = push(99);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(outcome->evicted.has_value());
  EXPECT_EQ(ring().pending(), kDepth);
}

TEST_F(IndexRingTest, AWonPopLeavesItsClaimStagedUntilWithdrawn) {
  ASSERT_TRUE(push(7).has_value());
  const core::expected<std::uint32_t> value = ring().pop(stage_);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 7U);
  // The claim is the crash evidence: it must name the entry until the
  // caller files it and withdraws.
  EXPECT_EQ(IndexRing::claimed_value(stage_.load()), 7U);
  stage_.store(kStageEmpty);
}

TEST_F(IndexRingTest, EvictionErasesTheLosersStaleClaim) {
  for (std::uint32_t value = 1; value <= kDepth; ++value) {
    ASSERT_TRUE(push(value).has_value());
  }
  // A consumer staked entry 1 at position 0 and died right after losing the
  // race it never actually ran; the eviction that wins position 0 must not
  // leave that claim looking like ownership.
  stage_.store(IndexRing::claim(1, 0));
  const core::expected<PushOutcome> outcome = ring().push(4, stage_);
  ASSERT_TRUE(outcome.has_value());
  const std::optional<std::uint32_t> evicted = outcome->evicted;
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(evicted.value_or(0U), 1U);
  EXPECT_EQ(stage_.load(), kStageEmpty);
}

TEST_F(IndexRingTest, InterleavesPushesAndPops) {
  std::vector<std::uint32_t> taken;
  std::uint32_t next = 0;
  for (std::size_t round = 0; round < 100; ++round) {
    ASSERT_TRUE(push(next).has_value());
    next += 1;
    if (round % 2 == 0) {
      const core::expected<std::uint32_t> popped = pop();
      ASSERT_TRUE(popped.has_value());
      taken.push_back(*popped);
    }
  }
  // Order is preserved among what the consumer saw, whatever was evicted.
  for (std::size_t position = 1; position < taken.size(); ++position) {
    EXPECT_LT(taken[position - 1], taken[position]);
  }
}

} // namespace
} // namespace volt::ipc::detail
