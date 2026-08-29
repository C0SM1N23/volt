#pragma once

#include "volt/log/argument_type.hpp"
#include "volt/log/level.hpp"
#include "volt/log/module.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::log {

/// Encodes one record into a slot, on the caller's thread.
///
/// Everything here is a byte store into memory the caller already owns: no
/// allocation, no formatting, no locks. That is the whole reason a log call on
/// the control path costs tens of nanoseconds rather than microseconds — the
/// text is assembled later, off the critical path, from the format id.
///
/// Overflow is not an error the caller handles: a record that does not fit its
/// slot is abandoned and `finish` reports zero, which the ring counts as a
/// drop. Refusing to log is always better than stalling a control cycle.
class RecordWriter final {
public:
  /// @pre `slot` outlives this writer and is at least kRecordHeaderBytes long
  explicit RecordWriter(std::span<std::byte> slot) noexcept : slot_{slot} {}

  /// Writes the fixed part of the record.
  ///
  /// @post the writer is positioned for arguments
  void begin(std::uint64_t identifier, std::int64_t timestamp_ns, Level level,
             Module module) noexcept;

  /// Appends a signed integer argument.
  void add(std::int64_t value) noexcept;

  /// Appends an unsigned integer argument.
  void add(std::uint64_t value) noexcept;

  /// Appends a floating-point argument.
  void add(double value) noexcept;

  /// Appends text, truncated to kMaxTextArgumentBytes.
  ///
  /// @pre the characters only have to stay alive for the call; they are copied
  void add(std::string_view value) noexcept;

  /// Finishes the record and returns its length.
  ///
  /// @post returns zero when anything did not fit, and the slot is then not a
  ///       valid record
  [[nodiscard]] std::size_t finish() noexcept;

private:
  [[nodiscard]] bool reserve(std::size_t bytes) noexcept;
  void put_tag(ArgumentType type) noexcept;

  std::span<std::byte> slot_;
  std::size_t offset_ = 0;
  std::size_t argument_count_ = 0;
  bool overflowed_ = false;
};

} // namespace volt::log
