#pragma once

#include "endian.hpp"
#include "error.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace volt::core {

namespace detail {

/// Reports whether a field of `field_size` bytes fits at `offset`.
///
/// Written as a subtraction on the buffer size rather than `offset +
/// field_size`, because the sum of two attacker-chosen sizes can wrap and
/// then compare as fitting.
[[nodiscard]] constexpr bool field_fits(std::size_t buffer_size, std::size_t offset,
                                        std::size_t field_size) noexcept {
  return offset <= buffer_size && buffer_size - offset >= field_size;
}

} // namespace detail

/// Reads the big-endian `T` stored at `offset`.
///
/// @pre    `buffer` only has to stay alive for the call; the value is copied out
/// @errors kInternalBufferTooSmall when the field does not fit in the buffer
template <std::integral T>
[[nodiscard]] constexpr expected<T> read_big_endian(std::span<const std::byte> buffer,
                                                    std::size_t offset) noexcept {
  using Raw = std::make_unsigned_t<T>;
  if (!detail::field_fits(buffer.size(), offset, sizeof(T))) {
    return std::unexpected{ErrorCode::kInternalBufferTooSmall};
  }
  Raw raw = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    raw = static_cast<Raw>(static_cast<Raw>(raw << kBitsPerByte) |
                           std::to_integer<std::uint8_t>(buffer[offset + i]));
  }
  return static_cast<T>(raw);
}

/// Reads the little-endian `T` stored at `offset`.
///
/// @pre    `buffer` only has to stay alive for the call; the value is copied out
/// @errors kInternalBufferTooSmall when the field does not fit in the buffer
template <std::integral T>
[[nodiscard]] constexpr expected<T> read_little_endian(std::span<const std::byte> buffer,
                                                       std::size_t offset) noexcept {
  // The bytes are assembled most-significant-first above, so reading the same
  // bytes as little-endian is that result with its byte order reversed.
  return read_big_endian<T>(buffer, offset).transform([](T assembled) noexcept {
    return static_cast<T>(std::byteswap(assembled));
  });
}

/// Writes `value` at `offset` in big-endian order.
///
/// @pre    `buffer` only has to stay alive for the call
/// @post   `sizeof(T)` bytes from `offset` hold `value`; on failure the buffer
///         is untouched, because the size is checked before the first store
/// @errors kInternalBufferTooSmall when the field does not fit in the buffer
template <std::integral T>
[[nodiscard]] constexpr expected<void> write_big_endian(std::span<std::byte> buffer,
                                                        std::size_t offset, T value) noexcept {
  using Raw = std::make_unsigned_t<T>;
  if (!detail::field_fits(buffer.size(), offset, sizeof(T))) {
    return std::unexpected{ErrorCode::kInternalBufferTooSmall};
  }
  const auto raw = static_cast<Raw>(value);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto shift =
        static_cast<unsigned>(kBitsPerByte) * static_cast<unsigned>(sizeof(T) - 1 - i);
    buffer[offset + i] = static_cast<std::byte>(static_cast<std::uint8_t>(raw >> shift));
  }
  return {};
}

/// Writes `value` at `offset` in little-endian order.
///
/// @pre    `buffer` only has to stay alive for the call
/// @post   `sizeof(T)` bytes from `offset` hold `value`; on failure the buffer
///         is untouched
/// @errors kInternalBufferTooSmall when the field does not fit in the buffer
template <std::integral T>
[[nodiscard]] constexpr expected<void> write_little_endian(std::span<std::byte> buffer,
                                                           std::size_t offset, T value) noexcept {
  return write_big_endian<T>(buffer, offset, static_cast<T>(std::byteswap(value)));
}

} // namespace volt::core
