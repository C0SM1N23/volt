#pragma once

#include <compare>
#include <cstddef>

namespace volt::memory {

/// Counts bytes without allowing an element count to be passed by accident.
class ByteCount final {
public:
  /// Constructs a zero-byte count.
  constexpr ByteCount() noexcept = default;

  /// Builds a count from bytes.
  [[nodiscard]] static constexpr ByteCount from_bytes(std::size_t bytes) noexcept {
    return ByteCount{bytes};
  }

  /// Returns the count in bytes.
  [[nodiscard]] constexpr std::size_t bytes() const noexcept { return bytes_; }

  /// Orders and compares byte counts.
  [[nodiscard]] constexpr auto operator<=>(const ByteCount &) const noexcept = default;
  [[nodiscard]] constexpr bool operator==(const ByteCount &) const noexcept = default;

private:
  constexpr explicit ByteCount(std::size_t bytes) noexcept : bytes_{bytes} {}

  std::size_t bytes_{};
};

} // namespace volt::memory
