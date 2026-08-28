#include "volt/core/duration.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace volt::core {
namespace {

// A span must never appear out of a bare number: the unit has to be written
// at the call site.
static_assert(!std::is_convertible_v<std::int64_t, Duration>);
static_assert(!std::is_convertible_v<Duration, std::int64_t>);

template <typename To, typename From>
concept AssignableFrom = requires(To target, From source) { target = source; };

static_assert(AssignableFrom<Duration, Duration>);
static_assert(!AssignableFrom<Duration, std::int64_t>);

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

struct UnitCase {
  std::string_view name;
  Duration value;
  std::int64_t expected_ns;
};

class DurationUnitTest : public ::testing::TestWithParam<UnitCase> {};

TEST_P(DurationUnitTest, ConvertsItsUnitToNanoseconds) {
  EXPECT_EQ(GetParam().value.ns(), GetParam().expected_ns);
}

INSTANTIATE_TEST_SUITE_P(
    Units, DurationUnitTest,
    ::testing::Values(UnitCase{"Nanoseconds", Duration::from_ns(7), 7},
                      UnitCase{"Microseconds", Duration::from_us(7), 7'000},
                      UnitCase{"Milliseconds", Duration::from_ms(7), 7'000'000},
                      UnitCase{"Seconds", Duration::from_s(7), 7'000'000'000},
                      UnitCase{"NegativeMilliseconds", Duration::from_ms(-7), -7'000'000},
                      UnitCase{"Zero", Duration{}, 0}),
    [](const ::testing::TestParamInfo<UnitCase> &param_info) {
      return std::string{param_info.param.name};
    });

TEST(DurationTest, IsUsableInConstantExpressions) {
  static_assert(Duration::from_ms(5).ns() == 5'000'000);
  static_assert(Duration::from_ms(1) < Duration::from_s(1));
  SUCCEED();
}

TEST(DurationTest, OrdersByLength) {
  EXPECT_LT(Duration::from_us(999), Duration::from_ms(1));
  EXPECT_EQ(Duration::from_ms(1), Duration::from_us(1'000));
  EXPECT_GT(Duration::from_s(1), Duration::from_ms(999));
  EXPECT_LT(Duration::from_ms(-1), Duration{});
}

TEST(DurationTest, AddsAndSubtractsBoundedSpans) {
  EXPECT_EQ(Duration::from_ms(3) + Duration::from_ms(4), Duration::from_ms(7));
  EXPECT_EQ(Duration::from_ms(3) - Duration::from_ms(4), Duration::from_ms(-1));
  EXPECT_EQ(-Duration::from_ms(3), Duration::from_ms(-3));
}

TEST(DurationTest, ReportsOverflowWhenAdditionLeavesTheRange) {
  const expected<Duration> sum = Duration::from_ns(kInt64Max).checked_add(Duration::from_ns(1));

  ASSERT_FALSE(sum.has_value());
  EXPECT_EQ(sum.error(), ErrorCode::kInternalArithmeticOverflow);
}

TEST(DurationTest, ReportsOverflowWhenSubtractionLeavesTheRange) {
  const expected<Duration> difference =
      Duration::from_ns(kInt64Min).checked_sub(Duration::from_ns(1));

  ASSERT_FALSE(difference.has_value());
  EXPECT_EQ(difference.error(), ErrorCode::kInternalArithmeticOverflow);
}

TEST(DurationTest, ReportsOverflowWhenScalingLeavesTheRange) {
  constexpr std::int64_t kFactor = 2;
  const expected<Duration> scaled = Duration::from_ns(kInt64Max).checked_mul(kFactor);

  ASSERT_FALSE(scaled.has_value());
  EXPECT_EQ(scaled.error(), ErrorCode::kInternalArithmeticOverflow);
}

TEST(DurationTest, KeepsTheResultWhenCheckedArithmeticStaysInRange) {
  const expected<Duration> sum = Duration::from_ms(3).checked_add(Duration::from_ms(4));

  ASSERT_TRUE(sum.has_value());
  EXPECT_EQ(sum->ns(), Duration::from_ms(7).ns());
}

TEST(DurationTest, ScalesWithinRange) {
  constexpr std::int64_t kRetryCount = 3;
  const expected<Duration> total = Duration::from_ms(10).checked_mul(kRetryCount);

  ASSERT_TRUE(total.has_value());
  EXPECT_EQ(*total, Duration::from_ms(30));
}

} // namespace
} // namespace volt::core
