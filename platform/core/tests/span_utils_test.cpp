#include "volt/core/span_utils.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace volt::core {
namespace {

constexpr std::size_t kBufferBytes = 8;

TEST(SpanUtilsTest, ReadsABigEndianFieldMostSignificantByteFirst) {
  constexpr std::array<std::byte, 4> kBuffer{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                             std::byte{0x04}};
  const expected<std::uint32_t> value = read_big_endian<std::uint32_t>(kBuffer, 0);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0x01020304U);
}

TEST(SpanUtilsTest, ReadsALittleEndianFieldLeastSignificantByteFirst) {
  constexpr std::array<std::byte, 4> kBuffer{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                             std::byte{0x04}};
  const expected<std::uint32_t> value = read_little_endian<std::uint32_t>(kBuffer, 0);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0x04030201U);
}

TEST(SpanUtilsTest, ReadsFromAnOffsetInsideTheBuffer) {
  constexpr std::array<std::byte, 4> kBuffer{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xAB},
                                             std::byte{0xCD}};
  const expected<std::uint16_t> value = read_big_endian<std::uint16_t>(kBuffer, 2);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0xABCDU);
}

TEST(SpanUtilsTest, RoundTripsEveryWidthThroughBigEndian) {
  std::array<std::byte, kBufferBytes> buffer{};
  const std::span<std::byte> writable{buffer};

  ASSERT_TRUE(write_big_endian<std::uint64_t>(writable, 0, 0x0102030405060708ULL).has_value());
  const expected<std::uint64_t> value = read_big_endian<std::uint64_t>(buffer, 0);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0x0102030405060708ULL);
}

TEST(SpanUtilsTest, RoundTripsEveryWidthThroughLittleEndian) {
  std::array<std::byte, kBufferBytes> buffer{};
  const std::span<std::byte> writable{buffer};

  ASSERT_TRUE(write_little_endian<std::uint64_t>(writable, 0, 0x0102030405060708ULL).has_value());
  const expected<std::uint64_t> value = read_little_endian<std::uint64_t>(buffer, 0);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0x0102030405060708ULL);
}

TEST(SpanUtilsTest, RoundTripsSignedValuesIncludingNegativeOnes) {
  std::array<std::byte, kBufferBytes> buffer{};
  const std::span<std::byte> writable{buffer};
  constexpr std::int32_t kNegative = -12345;

  ASSERT_TRUE(write_big_endian<std::int32_t>(writable, 0, kNegative).has_value());
  const expected<std::int32_t> value = read_big_endian<std::int32_t>(buffer, 0);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kNegative);
}

TEST(SpanUtilsTest, WritesBigEndianBytesInTheDeclaredOrder) {
  std::array<std::byte, 4> buffer{};
  const std::span<std::byte> writable{buffer};

  ASSERT_TRUE(write_big_endian<std::uint32_t>(writable, 0, 0xAABBCCDDU).has_value());

  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[0]), 0xAAU);
  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[1]), 0xBBU);
  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[2]), 0xCCU);
  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[3]), 0xDDU);
}

TEST(SpanUtilsTest, RefusesToReadAFieldThatCrossesTheEnd) {
  constexpr std::array<std::byte, 4> kBuffer{};
  const expected<std::uint32_t> value = read_big_endian<std::uint32_t>(kBuffer, 1);

  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ErrorCode::kInternalBufferTooSmall);
}

TEST(SpanUtilsTest, RefusesToReadFromAnOffsetPastTheEnd) {
  constexpr std::array<std::byte, 4> kBuffer{};
  const expected<std::uint8_t> value = read_big_endian<std::uint8_t>(kBuffer, kBufferBytes);

  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ErrorCode::kInternalBufferTooSmall);
}

TEST(SpanUtilsTest, RefusesAnOffsetLargeEnoughToWrapWhenAdded) {
  constexpr std::array<std::byte, 4> kBuffer{};
  const expected<std::uint32_t> value =
      read_big_endian<std::uint32_t>(kBuffer, std::numeric_limits<std::size_t>::max());

  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ErrorCode::kInternalBufferTooSmall);
}

TEST(SpanUtilsTest, RefusesToReadFromAnEmptyBuffer) {
  const expected<std::uint8_t> value =
      read_big_endian<std::uint8_t>(std::span<const std::byte>{}, 0);

  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ErrorCode::kInternalBufferTooSmall);
}

TEST(SpanUtilsTest, LeavesTheBufferUntouchedWhenAWriteDoesNotFit) {
  std::array<std::byte, 4> buffer{};
  const std::span<std::byte> writable{buffer};

  const expected<void> result = write_big_endian<std::uint32_t>(writable, 2, 0xAABBCCDDU);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ErrorCode::kInternalBufferTooSmall);
  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[2]), 0x00U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(buffer[3]), 0x00U);
}

TEST(SpanUtilsTest, ReadsAndWritesWhileCompiling) {
  static_assert([] {
    std::array<std::byte, 4> buffer{};
    const std::span<std::byte> writable{buffer};
    if (!write_big_endian<std::uint32_t>(writable, 0, 0x11223344U).has_value()) {
      return false;
    }
    const expected<std::uint32_t> value = read_big_endian<std::uint32_t>(buffer, 0);
    return value.has_value() && *value == 0x11223344U;
  }());
  SUCCEED();
}

} // namespace
} // namespace volt::core
