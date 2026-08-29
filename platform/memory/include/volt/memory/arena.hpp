#pragma once

#include "alignment.hpp"
#include "byte_count.hpp"

#include "volt/core/error.hpp"

#include <cstddef>
#include <span>

namespace volt::memory {

class FrameScope;

/// Carves aligned byte spans from caller-owned storage and reclaims them together.
///
/// The caller owns the backing bytes and must keep them alive and unmoved for
/// the lifetime of the arena and every span returned from it.
///
/// @thread one owning thread
/// @rt     allocation-free and bounded
class Arena final {
public:
  /// Builds an arena over caller-owned storage.
  /// @pre    `storage` remains alive and unmoved while the arena or its spans are used
  /// @thread initialization thread
  /// @rt     allocation-free
  Arena(std::span<std::byte> storage, Alignment alignment) noexcept;

  /// Reserves bytes using the arena's configured alignment.
  /// @post   the returned span remains valid until its enclosing reset or rewind
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kResourceExhausted when the backing storage cannot fit the request
  [[nodiscard]] core::expected<std::span<std::byte>> allocate(ByteCount bytes) noexcept;

  /// Reserves bytes using an alignment specific to this allocation.
  /// @post   the returned span remains valid until its enclosing reset or rewind
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kResourceExhausted when the backing storage cannot fit the request
  [[nodiscard]] core::expected<std::span<std::byte>> allocate(ByteCount bytes,
                                                              Alignment alignment) noexcept;

  /// Makes all backing storage available again.
  /// @post   every previously returned span is invalidated
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  void reset() noexcept;

  /// Returns the backing capacity in bytes.
  [[nodiscard]] ByteCount capacity_bytes() const noexcept;

  /// Returns the number of bytes crossed by the bump cursor.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] ByteCount used_bytes() const noexcept;

  /// Returns the alignment used by the single-argument `allocate`.
  [[nodiscard]] Alignment default_alignment() const noexcept;

  /// Returns how many allocation requests did not fit.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t allocation_failures() const noexcept;

private:
  friend class FrameScope;

  [[nodiscard]] ByteCount checkpoint() const noexcept;
  void rewind(ByteCount checkpoint) noexcept;

  std::span<std::byte> storage_;
  Alignment alignment_;
  std::size_t offset_bytes_ = 0;
  std::size_t allocation_failures_ = 0;
};

} // namespace volt::memory
