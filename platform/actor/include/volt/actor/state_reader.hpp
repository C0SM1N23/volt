#pragma once

#include "state_version.hpp"

#include "volt/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace volt {

/// Reads a bounded, versioned actor-state record in little-endian form.
/// @thread the actor's dispatcher thread
/// @rt     allocation-free; byte reads are bounded by caller-owned storage
class StateReader final {
public:
  /// Validates a state-record header and opens its payload.
  /// @pre    `record` remains alive and unmoved while the reader is used
  /// @post   successful reads begin immediately after the format header
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the header is truncated,
  ///         kTransientIntegrityCheckFailed when the format marker is wrong,
  ///         kExternalUnsupportedRequest when the schema version differs
  [[nodiscard]] static expected<StateReader> create(std::span<const std::byte> record,
                                                    StateVersion expected_version) noexcept;

  /// Reads an unsigned byte.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when no byte remains
  [[nodiscard]] expected<std::uint8_t> read_u8() noexcept;

  /// Reads a little-endian 16-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value is truncated
  [[nodiscard]] expected<std::uint16_t> read_u16() noexcept;

  /// Reads a little-endian 32-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value is truncated
  [[nodiscard]] expected<std::uint32_t> read_u32() noexcept;

  /// Reads a little-endian 64-bit value.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value is truncated
  [[nodiscard]] expected<std::uint64_t> read_u64() noexcept;

  /// Reads a little-endian signed 64-bit value without changing its bits.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the value is truncated
  [[nodiscard]] expected<std::int64_t> read_i64() noexcept;

  /// Takes an immutable view of the next `count_bytes` bytes.
  /// @post   the returned view remains valid while the input record does
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalBufferTooSmall when the requested bytes are truncated
  [[nodiscard]] expected<std::span<const std::byte>> read_bytes(std::size_t count_bytes) noexcept;

  /// Reports whether every payload byte has been consumed.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] bool finished() const noexcept;

  /// Returns how many payload bytes remain unread.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t remaining_bytes() const noexcept;

  /// Returns the schema version read from the record header.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] StateVersion version() const noexcept;

private:
  StateReader(std::span<const std::byte> record, StateVersion version) noexcept;

  [[nodiscard]] expected<std::uint64_t> read_integral(std::size_t width_bytes) noexcept;

  std::span<const std::byte> record_;
  std::size_t offset_bytes_ = 0;
  StateVersion version_{};
};

} // namespace volt
