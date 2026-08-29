#pragma once

#include <cstddef>
#include <cstdint>

namespace volt::log {

/// How one logged argument is encoded in a record.
///
/// The tag travels with every argument so a decoder can read a record without
/// knowing the format string, which is what lets a log be inspected even when
/// its format table is incomplete. Values are assigned once: they are on the
/// wire, and renumbering would reinterpret old captures.
enum class ArgumentType : std::uint8_t {
  kSigned = 1,
  kUnsigned = 2,
  kDouble = 3,
  kBoolean = 4,
  kText = 5,
};

/// Bytes of the fixed part of a record: length, level, module, argument count,
/// padding to eight, format id and timestamp.
inline constexpr std::size_t kRecordHeaderBytes = 24;

/// Longest text argument kept in a record.
///
/// Text is copied rather than pointed at, because a pointer means nothing to a
/// decoder in another process. A cap keeps the copy bounded on the caller
/// path; anything longer is truncated, which is preferable to a record that
/// cannot fit its slot.
inline constexpr std::size_t kMaxTextArgumentBytes = 64;

/// Most arguments one record can carry.
///
/// A format string with more placeholders than this is a message that wants to
/// be several messages.
inline constexpr std::size_t kMaxArguments = 8;

} // namespace volt::log
