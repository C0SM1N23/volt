#include "volt/log/log_file_header.hpp"

#include "volt/core/error.hpp"
#include "volt/core/span_utils.hpp"

#include <cstring>

namespace volt::log {
namespace {

// A format string longer than this is a message that should be split, and the
// cap keeps one length field enough to describe it.
constexpr std::size_t kMaxStoredTextBytes = 1024;

void append(std::vector<std::byte> &output, std::string_view text) {
  for (const char character : text) {
    output.push_back(static_cast<std::byte>(character));
  }
}

template <typename T> void append_value(std::vector<std::byte> &output, T value) {
  std::array<std::byte, sizeof(T)> encoded{};
  const core::expected<void> written = core::write_little_endian<T>(encoded, 0, value);
  VOLT_ASSERT(written.has_value(), "header field did not fit its own size");
  output.insert(output.end(), encoded.begin(), encoded.end());
}

[[nodiscard]] std::string_view view_of(const char *text) noexcept {
  return text == nullptr ? std::string_view{} : std::string_view{text};
}

} // namespace

std::vector<std::byte> build_log_file_header(std::span<const FormatEntry> formats) {
  std::vector<std::byte> header;
  append(header, kLogFileMagic);
  append_value<std::uint32_t>(header, static_cast<std::uint32_t>(formats.size()));

  for (const FormatEntry &entry : formats) {
    const std::string_view format = view_of(entry.format).substr(0, kMaxStoredTextBytes);
    const std::string_view file = view_of(entry.file).substr(0, kMaxStoredTextBytes);
    append_value<std::uint64_t>(header, entry.id);
    append_value<std::uint32_t>(header, entry.line);
    append_value<std::uint16_t>(header, static_cast<std::uint16_t>(format.size()));
    append_value<std::uint16_t>(header, static_cast<std::uint16_t>(file.size()));
    append(header, format);
    append(header, file);
  }
  return header;
}

namespace {

/// Reads one table entry and advances past it.
[[nodiscard]] core::expected<DecodedFormat> read_one_format(std::span<const std::byte> content,
                                                            std::size_t &offset) {
  const core::expected<std::uint64_t> identifier =
      core::read_little_endian<std::uint64_t>(content, offset);
  const core::expected<std::uint32_t> line =
      core::read_little_endian<std::uint32_t>(content, offset + sizeof(std::uint64_t));
  const core::expected<std::uint16_t> format_length = core::read_little_endian<std::uint16_t>(
      content, offset + sizeof(std::uint64_t) + sizeof(std::uint32_t));
  const core::expected<std::uint16_t> file_length = core::read_little_endian<std::uint16_t>(
      content, offset + sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint16_t));
  if (!identifier.has_value() || !line.has_value() || !format_length.has_value() ||
      !file_length.has_value()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  offset += sizeof(std::uint64_t) + sizeof(std::uint32_t) + (2 * sizeof(std::uint16_t));
  const auto text_bytes =
      static_cast<std::size_t>(*format_length) + static_cast<std::size_t>(*file_length);
  if (content.size() - offset < text_bytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  // `char` may alias any object representation, which is what these stored
  // bytes are; the views point into the caller's buffer.
  const auto *const text = reinterpret_cast<const char *>(content.data() + offset);
  offset += text_bytes;
  return DecodedFormat{.id = *identifier,
                       .line = *line,
                       .format = std::string_view{text, *format_length},
                       .file = std::string_view{text + *format_length, *file_length}};
}

} // namespace

core::expected<std::vector<DecodedFormat>> parse_log_file_header(std::span<const std::byte> content,
                                                                 std::size_t &consumed) {
  if (content.size() < kLogFileMagic.size()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  // `char` may alias any object representation; this is the file's magic.
  const std::string_view magic{reinterpret_cast<const char *>(content.data()),
                               kLogFileMagic.size()};
  if (magic != kLogFileMagic) {
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }

  std::size_t offset = kLogFileMagic.size();
  const core::expected<std::uint32_t> count =
      core::read_little_endian<std::uint32_t>(content, offset);
  if (!count.has_value()) {
    return std::unexpected{count.error()};
  }
  offset += sizeof(std::uint32_t);

  std::vector<DecodedFormat> formats;
  formats.reserve(*count);
  for (std::uint32_t index = 0; index < *count; ++index) {
    const core::expected<DecodedFormat> entry = read_one_format(content, offset);
    if (!entry.has_value()) {
      return std::unexpected{entry.error()};
    }
    formats.push_back(*entry);
  }

  consumed = offset;
  return formats;
}

} // namespace volt::log
