#pragma once

#include "state_version.hpp"

#include "volt/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace volt {

/// Writes a bounded, versioned actor-state record in little-endian form.
/// @thread the actor's dispatcher thread
/// @rt     allocation-free; byte writes are bounded by caller-owned storage
class StateWriter final {
public:
  /// Starts a state record in caller-owned storage.
  /// @pre    `storage` remains alive and unmoved through `finish`
  /// @post   the format header and `version` occupy the record prefix
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the format header does not fit
  [[nodiscard]] static expected<StateWriter> create(std::span<std::byte> storage,
                                                    StateVersion version) noexcept;

  /// Appends an unsigned byte.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when no byte remains
  [[nodiscard]] expected<void> write_u8(std::uint8_t value) noexcept;

  /// Appends a little-endian 16-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value does not fit
  [[nodiscard]] expected<void> write_u16(std::uint16_t value) noexcept;

  /// Appends a little-endian 32-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value does not fit
  [[nodiscard]] expected<void> write_u32(std::uint32_t value) noexcept;

  /// Appends a little-endian 64-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value does not fit
  [[nodiscard]] expected<void> write_u64(std::uint64_t value) noexcept;

  /// Appends a little-endian signed 64-bit value without changing its bits.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value does not fit
  [[nodiscard]] expected<void> write_i64(std::int64_t value) noexcept;

  /// Appends bytes exactly as supplied.
  /// @pre    `bytes` remains valid only for this call; nothing is retained
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by `bytes.size()`
  /// @errors kInternalBufferTooSmall when the bytes do not fit
  [[nodiscard]] expected<void> write_bytes(std::span<const std::byte> bytes) noexcept;

  /// Returns the complete record written so far.
  /// @post   the returned view remains valid while the caller-owned storage does
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::span<const std::byte> finish() const noexcept;

  /// Returns how many payload bytes can still be appended.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t remaining_bytes() const noexcept;

  /// Returns the schema version stored in the record header.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] StateVersion version() const noexcept;

private:
  StateWriter(std::span<std::byte> storage, StateVersion version) noexcept;

  [[nodiscard]] expected<void> append_integral(std::uint64_t value,
                                               std::size_t width_bytes) noexcept;

  std::span<std::byte> storage_;
  std::size_t offset_bytes_ = 0;
  StateVersion version_{};
};

} // namespace volt
