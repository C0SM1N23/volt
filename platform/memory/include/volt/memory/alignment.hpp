#pragma once

#include "volt/core/error.hpp"

#include "byte_count.hpp"

#include <bit>
#include <cstddef>

namespace volt::memory {

/// Describes a valid power-of-two byte alignment.
class Alignment final {
public:
  /// Validates and builds an alignment.
  /// @errors kConfigValueOutOfRange when `bytes` is zero or not a power of two
  [[nodiscard]] static constexpr core::expected<Alignment> create(ByteCount bytes) noexcept {
    if (!std::has_single_bit(bytes.bytes())) {
      return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
    }
    return Alignment{bytes};
  }

  /// Returns the alignment in bytes.
  [[nodiscard]] constexpr ByteCount bytes() const noexcept { return bytes_; }

private:
  constexpr explicit Alignment(ByteCount bytes) noexcept : bytes_{bytes} {}

  ByteCount bytes_{};
};

} // namespace volt::memory
