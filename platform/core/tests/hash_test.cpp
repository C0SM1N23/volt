#include "volt/core/hash.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace volt::core {
namespace {

// Arbitrary but fixed byte sequence. The reference digests below were produced
// from exactly this sequence by the upstream xxHash implementation (v0.8.2),
// so the stride and offset are part of the expected values.
constexpr std::size_t kPatternStride = 7;
constexpr std::size_t kPatternOffset = 1;
constexpr std::size_t kPatternBytes = 64;

constexpr std::array<std::byte, kPatternBytes> kPattern = [] {
  std::array<std::byte, kPatternBytes> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>((i * kPatternStride) + kPatternOffset);
  }
  return bytes;
}();

struct ByteVector {
  std::string_view name;
  std::size_t length;
  std::uint64_t seed;
  std::uint64_t expected_digest;
};

class XxHash64ByteVectorTest : public ::testing::TestWithParam<ByteVector> {};

TEST_P(XxHash64ByteVectorTest, MatchesTheReferenceImplementation) {
  const std::span<const std::byte> input{kPattern.data(), GetParam().length};
  EXPECT_EQ(xxhash64(input, GetParam().seed), GetParam().expected_digest);
}

// Lengths chosen to exercise every branch: below one stripe, exactly one
// stripe, several stripes, a tail that is not a multiple of eight, and a
// non-zero seed.
INSTANTIATE_TEST_SUITE_P(
    ReferenceVectors, XxHash64ByteVectorTest,
    ::testing::Values(ByteVector{"Empty", 0, 0, 0xEF46DB3751D8E999ULL},
                      ByteVector{"OneWord", 8, 0, 0xC6F1803A5E0B3222ULL},
                      ByteVector{"ExactlyOneStripe", 32, 0, 0x5A0756FBE9ECD3D1ULL},
                      ByteVector{"StripeWithPartialTail", 41, 0, 0x8282A61B6BE9C67CULL},
                      ByteVector{"TwoStripes", 64, 0, 0x90083DA9CDB9D795ULL},
                      ByteVector{"TwoStripesSeeded", 64, 0xDEADBEEFULL, 0xF2E2CFDF885550F8ULL}),
    [](const ::testing::TestParamInfo<ByteVector> &param_info) {
      return std::string{param_info.param.name};
    });

struct TextVector {
  std::string_view name;
  std::string_view text;
  std::uint64_t seed;
  std::uint64_t expected_digest;
};

class XxHash64TextVectorTest : public ::testing::TestWithParam<TextVector> {};

TEST_P(XxHash64TextVectorTest, MatchesTheReferenceImplementation) {
  EXPECT_EQ(xxhash64(GetParam().text, GetParam().seed), GetParam().expected_digest);
}

INSTANTIATE_TEST_SUITE_P(
    ReferenceVectors, XxHash64TextVectorTest,
    ::testing::Values(TextVector{"EmptyString", "", 0, 0xEF46DB3751D8E999ULL},
                      TextVector{"SingleCharacter", "a", 0, 0xD24EC4F1A98C6E5BULL},
                      TextVector{"ThreeCharacters", "abc", 0, 0x44BC2CF5AD770999ULL},
                      TextVector{"FourteenCharacters", "message digest", 0, 0x066ED728FCEEB3BEULL},
                      TextVector{"SeededThreeCharacters", "abc", 1, 0xBEA9CA8199328908ULL}),
    [](const ::testing::TestParamInfo<TextVector> &param_info) {
      return std::string{param_info.param.name};
    });

TEST(XxHash64Test, ProducesLogFormatIdentifiersWhileCompiling) {
  static_assert(xxhash64(std::string_view{"abc"}) == 0x44BC2CF5AD770999ULL);
  static_assert(xxhash64(std::string_view{""}) == 0xEF46DB3751D8E999ULL);
  SUCCEED();
}

TEST(XxHash64Test, SeparatesInputsThatDifferInASingleByte) {
  EXPECT_NE(xxhash64(std::string_view{"abc"}), xxhash64(std::string_view{"abd"}));
}

TEST(XxHash64Test, SeparatesSeedsOverTheSameInput) {
  EXPECT_NE(xxhash64(std::string_view{"abc"}, 0), xxhash64(std::string_view{"abc"}, 1));
}

} // namespace
} // namespace volt::core
