#include "volt/core/timestamp.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace volt::core {
namespace {

// A point on the time base is not a number and not a span. Both confusions
// would compile silently if these conversions existed.
static_assert(!std::is_convertible_v<std::int64_t, Timestamp>);
static_assert(!std::is_convertible_v<Timestamp, std::int64_t>);
static_assert(!std::is_convertible_v<Duration, Timestamp>);
static_assert(!std::is_convertible_v<Timestamp, Duration>);

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

TEST(TimestampTest, DefaultConstructsToTheClusterEpoch) {
  constexpr Timestamp epoch{};
  EXPECT_EQ(epoch.ns_since_epoch(), 0);
}

TEST(TimestampTest, KeepsTheDistanceItWasConstructedWith) {
  constexpr std::int64_t kNanoseconds = 1'500'000'000;
  EXPECT_EQ(Timestamp::from_ns_since_epoch(kNanoseconds).ns_since_epoch(), kNanoseconds);
}

TEST(TimestampTest, IsUsableInConstantExpressions) {
  static_assert(Timestamp::from_ns_since_epoch(5).ns_since_epoch() == 5);
  static_assert(Timestamp::from_ns_since_epoch(1) < Timestamp::from_ns_since_epoch(2));
  SUCCEED();
}

TEST(TimestampTest, OrdersByPositionOnTheTimeBase) {
  EXPECT_LT(Timestamp::from_ns_since_epoch(1), Timestamp::from_ns_since_epoch(2));
  EXPECT_EQ(Timestamp::from_ns_since_epoch(3), Timestamp::from_ns_since_epoch(3));
  EXPECT_GT(Timestamp::from_ns_since_epoch(0), Timestamp::from_ns_since_epoch(-1));
}

TEST(TimestampTest, MovesForwardByASpan) {
  const expected<Timestamp> moved =
      Timestamp::from_ns_since_epoch(1'000).checked_add(Duration::from_us(1));

  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved->ns_since_epoch(), 2'000);
}

TEST(TimestampTest, MovesBackwardByASpan) {
  const expected<Timestamp> moved =
      Timestamp::from_ns_since_epoch(1'000).checked_sub(Duration::from_ns(400));

  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved->ns_since_epoch(), 600);
}

TEST(TimestampTest, MeasuresTheSpanBetweenTwoPoints) {
  const expected<Duration> elapsed = Timestamp::from_ns_since_epoch(5'000'000).checked_since(
      Timestamp::from_ns_since_epoch(1'000'000));

  ASSERT_TRUE(elapsed.has_value());
  EXPECT_EQ(*elapsed, Duration::from_ms(4));
}

TEST(TimestampTest, ReportsANegativeSpanWhenTheOrderIsReversed) {
  const expected<Duration> elapsed = Timestamp::from_ns_since_epoch(1'000'000).checked_since(
      Timestamp::from_ns_since_epoch(5'000'000));

  ASSERT_TRUE(elapsed.has_value());
  EXPECT_EQ(*elapsed, Duration::from_ms(-4));
}

TEST(TimestampTest, ReportsOverflowWhenMovingForwardLeavesTheRange) {
  const expected<Timestamp> moved =
      Timestamp::from_ns_since_epoch(kInt64Max).checked_add(Duration::from_ns(1));

  ASSERT_FALSE(moved.has_value());
  EXPECT_EQ(moved.error(), ErrorCode::kInternalArithmeticOverflow);
}

TEST(TimestampTest, ReportsOverflowWhenMovingBackwardLeavesTheRange) {
  const expected<Timestamp> moved =
      Timestamp::from_ns_since_epoch(kInt64Min).checked_sub(Duration::from_ns(1));

  ASSERT_FALSE(moved.has_value());
  EXPECT_EQ(moved.error(), ErrorCode::kInternalArithmeticOverflow);
}

TEST(TimestampTest, ReportsOverflowWhenTheSpanBetweenPointsLeavesTheRange) {
  const expected<Duration> elapsed =
      Timestamp::from_ns_since_epoch(kInt64Max).checked_since(Timestamp::from_ns_since_epoch(-1));

  ASSERT_FALSE(elapsed.has_value());
  EXPECT_EQ(elapsed.error(), ErrorCode::kInternalArithmeticOverflow);
}

} // namespace
} // namespace volt::core
