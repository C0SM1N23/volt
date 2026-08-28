#include "volt/core/endian.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <random>

namespace volt::core {
namespace {

// Fixed so a failure is reproducible; printed with every failing assertion so
// the run that found it can be repeated exactly.
constexpr std::uint64_t kRoundTripSeed = 0x5EED'1234'ABCD'0001ULL;
constexpr int kRoundTripSamples = 10'000;

TEST(EndianTest, BigEndianPlacesTheMostSignificantByteFirst) {
  constexpr std::uint32_t kValue = 0x01020304U;
  const auto bytes = std::bit_cast<std::array<std::byte, sizeof(kValue)>>(to_big_endian(kValue));

  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[0]), 0x01U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[1]), 0x02U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[2]), 0x03U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[3]), 0x04U);
}

TEST(EndianTest, LittleEndianPlacesTheLeastSignificantByteFirst) {
  constexpr std::uint32_t kValue = 0x01020304U;
  const auto bytes = std::bit_cast<std::array<std::byte, sizeof(kValue)>>(to_little_endian(kValue));

  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[0]), 0x04U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[1]), 0x03U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[2]), 0x02U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[3]), 0x01U);
}

TEST(EndianTest, LeavesSingleByteValuesAlone) {
  constexpr std::uint8_t kValue = 0xA5U;
  EXPECT_EQ(to_big_endian(kValue), kValue);
  EXPECT_EQ(to_little_endian(kValue), kValue);
}

TEST(EndianTest, ConvertsWhileCompiling) {
  static_assert(from_big_endian(to_big_endian(std::uint16_t{0x1234U})) == 0x1234U);
  static_assert(from_little_endian(to_little_endian(std::uint64_t{0x0102030405060708ULL})) ==
                0x0102030405060708ULL);
  SUCCEED();
}

TEST(EndianTest, RoundTripsTenThousandRandomValuesThroughBigEndian) {
  std::mt19937_64 generator{kRoundTripSeed};
  std::uniform_int_distribution<std::uint64_t> distribution;

  for (int sample = 0; sample < kRoundTripSamples; ++sample) {
    const std::uint64_t value = distribution(generator);
    ASSERT_EQ(from_big_endian(to_big_endian(value)), value)
        << "seed=" << kRoundTripSeed << " sample=" << sample;
  }
}

TEST(EndianTest, RoundTripsTenThousandRandomValuesThroughLittleEndian) {
  std::mt19937_64 generator{kRoundTripSeed};
  std::uniform_int_distribution<std::uint64_t> distribution;

  for (int sample = 0; sample < kRoundTripSamples; ++sample) {
    const std::uint64_t value = distribution(generator);
    ASSERT_EQ(from_little_endian(to_little_endian(value)), value)
        << "seed=" << kRoundTripSeed << " sample=" << sample;
  }
}

TEST(EndianTest, RoundTripsTenThousandRandomSixteenBitValues) {
  std::mt19937_64 generator{kRoundTripSeed};
  std::uniform_int_distribution<std::uint16_t> distribution;

  for (int sample = 0; sample < kRoundTripSamples; ++sample) {
    const std::uint16_t value = distribution(generator);
    ASSERT_EQ(from_big_endian(to_big_endian(value)), value)
        << "seed=" << kRoundTripSeed << " sample=" << sample;
    ASSERT_EQ(from_little_endian(to_little_endian(value)), value)
        << "seed=" << kRoundTripSeed << " sample=" << sample;
  }
}

} // namespace
} // namespace volt::core
