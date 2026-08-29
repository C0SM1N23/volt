#include "volt/log/record_writer.hpp"

#include "volt/core/endian.hpp"
#include "volt/core/error.hpp"
#include "volt/core/span_utils.hpp"

#include <type_traits>

#include <algorithm>
#include <bit>
#include <cstring>

namespace volt::log {
namespace {

// Offsets inside the fixed part. Written out rather than derived from a struct
// so the layout on the wire is stated in one place and does not depend on how
// a compiler pads anything.
constexpr std::size_t kOffsetTotalBytes = 0;
constexpr std::size_t kOffsetLevel = 2;
constexpr std::size_t kOffsetModule = 3;
constexpr std::size_t kOffsetArgumentCount = 4;
constexpr std::size_t kOffsetFormatId = 8;
constexpr std::size_t kOffsetTimestamp = 16;

/// Stores one field at `offset`, little-endian.
///
/// A single copy rather than the byte loop of `span_utils`: this runs on the
/// caller's control path, where SPEC 8.4 budgets tens of nanoseconds for the
/// whole record, and eight bounds-checked byte loops do not fit in that. Room
/// was already reserved, so the assert states the invariant rather than
/// handling a case.
template <typename T> void store(std::span<std::byte> slot, std::size_t offset, T value) noexcept {
  VOLT_ASSERT(offset + sizeof(T) <= slot.size(), "record field written past its slot");
  const auto encoded = core::to_little_endian(static_cast<std::make_unsigned_t<T>>(value));
  std::memcpy(slot.data() + offset, &encoded, sizeof(encoded));
}

} // namespace

void RecordWriter::begin(std::uint64_t identifier, std::int64_t timestamp_ns, Level level,
                         Module module) noexcept {
  if (slot_.size() < kRecordHeaderBytes) {
    overflowed_ = true;
    return;
  }
  // The reserved bytes between the argument count and the format id keep the
  // two 64-bit fields aligned, which is what lets a decoder read them without
  // assembling them byte by byte.
  std::ranges::fill(slot_.first(kRecordHeaderBytes), std::byte{0});
  store<std::uint8_t>(slot_, kOffsetLevel, static_cast<std::uint8_t>(level));
  store<std::uint8_t>(slot_, kOffsetModule, static_cast<std::uint8_t>(module));
  store<std::uint64_t>(slot_, kOffsetFormatId, identifier);
  store<std::int64_t>(slot_, kOffsetTimestamp, timestamp_ns);
  offset_ = kRecordHeaderBytes;
}

bool RecordWriter::reserve(std::size_t bytes) noexcept {
  if (overflowed_ || argument_count_ >= kMaxArguments || slot_.size() - offset_ < bytes) {
    overflowed_ = true;
    return false;
  }
  return true;
}

void RecordWriter::put_tag(ArgumentType type) noexcept {
  store<std::uint8_t>(slot_, offset_, static_cast<std::uint8_t>(type));
  offset_ += 1;
}

void RecordWriter::add(std::int64_t value) noexcept {
  if (!reserve(1 + sizeof(std::int64_t))) {
    return;
  }
  put_tag(ArgumentType::kSigned);
  store<std::int64_t>(slot_, offset_, value);
  offset_ += sizeof(std::int64_t);
  argument_count_ += 1;
}

void RecordWriter::add(std::uint64_t value) noexcept {
  if (!reserve(1 + sizeof(std::uint64_t))) {
    return;
  }
  put_tag(ArgumentType::kUnsigned);
  store<std::uint64_t>(slot_, offset_, value);
  offset_ += sizeof(std::uint64_t);
  argument_count_ += 1;
}

void RecordWriter::add(double value) noexcept {
  if (!reserve(1 + sizeof(double))) {
    return;
  }
  put_tag(ArgumentType::kDouble);
  // The bit pattern is what travels; a decoder turns it back with the reverse
  // cast, so the value survives without the writer formatting anything.
  store<std::uint64_t>(slot_, offset_, std::bit_cast<std::uint64_t>(value));
  offset_ += sizeof(double);
  argument_count_ += 1;
}

void RecordWriter::add(std::string_view value) noexcept {
  const std::size_t length = std::min(value.size(), kMaxTextArgumentBytes);
  if (!reserve(1 + sizeof(std::uint16_t) + length)) {
    return;
  }
  put_tag(ArgumentType::kText);
  store<std::uint16_t>(slot_, offset_, static_cast<std::uint16_t>(length));
  offset_ += sizeof(std::uint16_t);
  VOLT_ASSERT(offset_ + length <= slot_.size(), "text argument written past its slot");
  std::memcpy(slot_.data() + offset_, value.data(), length);
  offset_ += length;
  argument_count_ += 1;
}

std::size_t RecordWriter::finish() noexcept {
  if (overflowed_ || offset_ < kRecordHeaderBytes) {
    return 0;
  }
  store<std::uint16_t>(slot_, kOffsetTotalBytes, static_cast<std::uint16_t>(offset_));
  store<std::uint8_t>(slot_, kOffsetArgumentCount, static_cast<std::uint8_t>(argument_count_));
  return offset_;
}

} // namespace volt::log
