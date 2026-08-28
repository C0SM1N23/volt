#pragma once

#include "endian.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::core {

namespace detail {

// Round constants of XXH64, taken from the xxHash specification
// (Cyan4973/xxHash, doc/xxhash_spec.md). They are the algorithm, not tuning
// parameters: changing one yields a digest no other xxHash implementation
// reproduces, which would break every recorded state hash.
inline constexpr std::uint64_t kXxhPrime1 = 11400714785074694791ULL;
inline constexpr std::uint64_t kXxhPrime2 = 14029467366897019727ULL;
inline constexpr std::uint64_t kXxhPrime3 = 1609587929392839161ULL;
inline constexpr std::uint64_t kXxhPrime4 = 9650029242287828579ULL;
inline constexpr std::uint64_t kXxhPrime5 = 2870177450012600261ULL;

// Rotation and shift amounts from the same specification, equally fixed.
inline constexpr int kXxhAccumulatorRotate = 31;
inline constexpr int kXxhLaneRotate1 = 1;
inline constexpr int kXxhLaneRotate2 = 7;
inline constexpr int kXxhLaneRotate3 = 12;
inline constexpr int kXxhLaneRotate4 = 18;
inline constexpr int kXxhTailWordRotate = 27;
inline constexpr int kXxhTailDwordRotate = 23;
inline constexpr int kXxhTailByteRotate = 11;
inline constexpr int kXxhAvalancheShift1 = 33;
inline constexpr int kXxhAvalancheShift2 = 29;
inline constexpr int kXxhAvalancheShift3 = 32;

// The bulk loop consumes one stripe of four 64-bit lanes per iteration.
inline constexpr std::size_t kXxhStripeBytes = 4 * sizeof(std::uint64_t);

[[nodiscard]] constexpr std::uint64_t xxh_round(std::uint64_t accumulator,
                                                std::uint64_t input) noexcept {
  const std::uint64_t mixed = accumulator + (input * kXxhPrime2);
  return std::rotl(mixed, kXxhAccumulatorRotate) * kXxhPrime1;
}

[[nodiscard]] constexpr std::uint64_t xxh_merge(std::uint64_t accumulator,
                                                std::uint64_t lane) noexcept {
  return ((accumulator ^ xxh_round(0, lane)) * kXxhPrime1) + kXxhPrime4;
}

[[nodiscard]] constexpr std::uint64_t xxh_avalanche(std::uint64_t hash) noexcept {
  hash ^= hash >> kXxhAvalancheShift1;
  hash *= kXxhPrime2;
  hash ^= hash >> kXxhAvalancheShift2;
  hash *= kXxhPrime3;
  hash ^= hash >> kXxhAvalancheShift3;
  return hash;
}

// `byte_at` yields the byte at an index. Keeping the algorithm generic over it is
// what lets the same code hash a byte span at run time and a string literal
// during constant evaluation, where reinterpreting chars as bytes is not
// allowed.
template <typename ByteAt>
[[nodiscard]] constexpr std::uint64_t xxh_word(ByteAt byte_at, std::size_t offset) noexcept {
  std::uint64_t word = 0;
  for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
    const auto shift = static_cast<unsigned>(kBitsPerByte) * static_cast<unsigned>(i);
    word |= static_cast<std::uint64_t>(byte_at(offset + i)) << shift;
  }
  return word;
}

template <typename ByteAt>
[[nodiscard]] constexpr std::uint32_t xxh_dword(ByteAt byte_at, std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
    const auto shift = static_cast<unsigned>(kBitsPerByte) * static_cast<unsigned>(i);
    value |= static_cast<std::uint32_t>(byte_at(offset + i)) << shift;
  }
  return value;
}

template <typename ByteAt>
[[nodiscard]] constexpr std::uint64_t xxhash64_impl(ByteAt byte_at, std::size_t length,
                                                    std::uint64_t seed) noexcept {
  constexpr std::size_t kWord = sizeof(std::uint64_t);
  std::uint64_t hash = 0;
  std::size_t offset = 0;

  if (length >= kXxhStripeBytes) {
    std::uint64_t lane1 = seed + kXxhPrime1 + kXxhPrime2;
    std::uint64_t lane2 = seed + kXxhPrime2;
    std::uint64_t lane3 = seed;
    std::uint64_t lane4 = seed - kXxhPrime1;
    const std::size_t last_stripe_start = length - kXxhStripeBytes;
    while (offset <= last_stripe_start) {
      lane1 = xxh_round(lane1, xxh_word(byte_at, offset));
      lane2 = xxh_round(lane2, xxh_word(byte_at, offset + kWord));
      lane3 = xxh_round(lane3, xxh_word(byte_at, offset + (2 * kWord)));
      lane4 = xxh_round(lane4, xxh_word(byte_at, offset + (3 * kWord)));
      offset += kXxhStripeBytes;
    }
    hash = std::rotl(lane1, kXxhLaneRotate1) + std::rotl(lane2, kXxhLaneRotate2) +
           std::rotl(lane3, kXxhLaneRotate3) + std::rotl(lane4, kXxhLaneRotate4);
    hash = xxh_merge(hash, lane1);
    hash = xxh_merge(hash, lane2);
    hash = xxh_merge(hash, lane3);
    hash = xxh_merge(hash, lane4);
  } else {
    hash = seed + kXxhPrime5;
  }

  hash += static_cast<std::uint64_t>(length);

  while (length - offset >= kWord) {
    hash ^= xxh_round(0, xxh_word(byte_at, offset));
    hash = (std::rotl(hash, kXxhTailWordRotate) * kXxhPrime1) + kXxhPrime4;
    offset += kWord;
  }
  if (length - offset >= sizeof(std::uint32_t)) {
    hash ^= static_cast<std::uint64_t>(xxh_dword(byte_at, offset)) * kXxhPrime1;
    hash = (std::rotl(hash, kXxhTailDwordRotate) * kXxhPrime2) + kXxhPrime3;
    offset += sizeof(std::uint32_t);
  }
  while (offset < length) {
    hash ^= static_cast<std::uint64_t>(byte_at(offset)) * kXxhPrime5;
    hash = std::rotl(hash, kXxhTailByteRotate) * kXxhPrime1;
    offset += 1;
  }
  return xxh_avalanche(hash);
}

} // namespace detail

/// Returns the XXH64 digest of `data`.
///
/// @pre `data` only has to stay alive for the call; nothing is retained.
/// @rt  allocation-free, bounded by the length of the input
[[nodiscard]] constexpr std::uint64_t xxhash64(std::span<const std::byte> data,
                                               std::uint64_t seed = 0) noexcept {
  return detail::xxhash64_impl(
      [data](std::size_t index) { return std::to_integer<std::uint8_t>(data[index]); }, data.size(),
      seed);
}

/// Returns the XXH64 digest of `text`, so that log format strings and other
/// literals can be turned into stable identifiers while compiling.
///
/// @pre `text` only has to stay alive for the call; nothing is retained.
[[nodiscard]] constexpr std::uint64_t xxhash64(std::string_view text,
                                               std::uint64_t seed = 0) noexcept {
  return detail::xxhash64_impl(
      [text](std::size_t index) { return static_cast<std::uint8_t>(text[index]); }, text.size(),
      seed);
}

} // namespace volt::core
