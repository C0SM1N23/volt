#pragma once

#include <bit>
#include <concepts>
#include <limits>

namespace volt::core {

/// Width of a byte in bits, used wherever multi-byte values are packed.
inline constexpr int kBitsPerByte = std::numeric_limits<unsigned char>::digits;

/// Returns `value` with its bytes in network order.
///
/// The conversion is always written out at the call site rather than hidden in
/// a serialiser, because a wrong byte order is invisible on the host that
/// produced it and only shows up on the peer.
template <std::integral T> [[nodiscard]] constexpr T to_big_endian(T value) noexcept {
  if constexpr (std::endian::native == std::endian::big) {
    return value;
  } else {
    return std::byteswap(value);
  }
}

/// Returns `value` with its bytes in little-endian order.
template <std::integral T> [[nodiscard]] constexpr T to_little_endian(T value) noexcept {
  if constexpr (std::endian::native == std::endian::little) {
    return value;
  } else {
    return std::byteswap(value);
  }
}

/// Reads a big-endian `value` back into host order.
///
/// A byte swap is its own inverse, so this is the same operation as
/// `to_big_endian`. Both names exist so that a call site states which
/// direction it is converting, which is the part a reader has to check.
template <std::integral T> [[nodiscard]] constexpr T from_big_endian(T value) noexcept {
  return to_big_endian(value);
}

/// Reads a little-endian `value` back into host order.
template <std::integral T> [[nodiscard]] constexpr T from_little_endian(T value) noexcept {
  return to_little_endian(value);
}

} // namespace volt::core
