#include "volt/log/record_reader.hpp"

#include "volt/core/span_utils.hpp"

#include <bit>
#include <string_view>

namespace volt::log {
namespace {

constexpr std::size_t kOffsetTotalBytes = 0;
constexpr std::size_t kOffsetLevel = 2;
constexpr std::size_t kOffsetModule = 3;
constexpr std::size_t kOffsetArgumentCount = 4;
constexpr std::size_t kOffsetFormatId = 8;
constexpr std::size_t kOffsetTimestamp = 16;

} // namespace

core::expected<void> RecordReader::parse_header() noexcept {
  if (record_.size() < kRecordHeaderBytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  const core::expected<std::uint16_t> total =
      core::read_little_endian<std::uint16_t>(record_, kOffsetTotalBytes);
  const core::expected<std::uint8_t> level =
      core::read_little_endian<std::uint8_t>(record_, kOffsetLevel);
  const core::expected<std::uint8_t> module =
      core::read_little_endian<std::uint8_t>(record_, kOffsetModule);
  const core::expected<std::uint8_t> arguments =
      core::read_little_endian<std::uint8_t>(record_, kOffsetArgumentCount);
  const core::expected<std::uint64_t> identifier =
      core::read_little_endian<std::uint64_t>(record_, kOffsetFormatId);
  const core::expected<std::int64_t> timestamp =
      core::read_little_endian<std::int64_t>(record_, kOffsetTimestamp);
  if (!total.has_value() || !level.has_value() || !module.has_value() || !arguments.has_value() ||
      !identifier.has_value() || !timestamp.has_value()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  // A length that runs past the buffer means the file was cut short, which is
  // what a full disk or a power cut leaves behind.
  if (*total < kRecordHeaderBytes || *total > record_.size()) {
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }
  if (*arguments > kMaxArguments) {
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }

  total_bytes_ = *total;
  level_ = static_cast<Level>(*level);
  module_ = static_cast<Module>(*module);
  argument_count_ = *arguments;
  format_id_ = *identifier;
  timestamp_ns_ = *timestamp;
  offset_ = kRecordHeaderBytes;
  return {};
}

namespace {

/// Reads one fixed-width field and advances past it.
template <typename T>
[[nodiscard]] core::expected<T> take(std::span<const std::byte> record,
                                     std::size_t &offset) noexcept {
  const core::expected<T> value = core::read_little_endian<T>(record, offset);
  if (!value.has_value()) {
    return std::unexpected{value.error()};
  }
  offset += sizeof(T);
  return *value;
}

/// Reads a length-prefixed text argument and advances past it.
[[nodiscard]] core::expected<std::string_view> take_text(std::span<const std::byte> record,
                                                         std::size_t &offset) noexcept {
  const core::expected<std::uint16_t> length = take<std::uint16_t>(record, offset);
  if (!length.has_value()) {
    return std::unexpected{length.error()};
  }
  if (record.size() - offset < *length) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  // Reading the stored bytes back as characters is the one aliasing this
  // decoder does: `char` is explicitly allowed to alias any object
  // representation, which is exactly what a byte buffer holding text is. The
  // view points into the record, which the caller promised outlives it.
  const auto *const text = reinterpret_cast<const char *>(record.data() + offset);
  offset += *length;
  return std::string_view{text, *length};
}

} // namespace

core::expected<Argument> RecordReader::next_argument() noexcept {
  const core::expected<std::uint8_t> tag = take<std::uint8_t>(record_, offset_);
  if (!tag.has_value()) {
    return std::unexpected{tag.error()};
  }

  switch (static_cast<ArgumentType>(*tag)) {
  case ArgumentType::kSigned:
    return take<std::int64_t>(record_, offset_).transform([](std::int64_t value) {
      return Argument{value};
    });
  case ArgumentType::kUnsigned:
    return take<std::uint64_t>(record_, offset_).transform([](std::uint64_t value) {
      return Argument{value};
    });
  case ArgumentType::kDouble:
    // The bit pattern is what travelled; the reverse cast turns it back.
    return take<std::uint64_t>(record_, offset_).transform([](std::uint64_t bits) {
      return Argument{std::bit_cast<double>(bits)};
    });
  case ArgumentType::kBoolean:
    return take<std::uint8_t>(record_, offset_).transform([](std::uint8_t value) {
      return Argument{value != 0};
    });
  case ArgumentType::kText:
    return take_text(record_, offset_).transform([](std::string_view value) {
      return Argument{value};
    });
  }
  return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
}

} // namespace volt::log
