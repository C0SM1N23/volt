#pragma once

#include "volt/core/error.hpp"
#include "volt/log/format_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace volt::log {

/// Bytes that begin every log file, so a decoder can tell one apart from any
/// other file and refuse a version it does not understand.
inline constexpr std::string_view kLogFileMagic = "VOLTLOG1";

/// Builds the header that starts a log file.
///
/// The format table is copied into every file rather than left in the
/// executable that produced it. A log outlives the build that wrote it: by the
/// time a capture from a vehicle is looked at, the binary has usually been
/// rebuilt and its format ids no longer match. A self-describing file stays
/// readable, and the few kilobytes it costs are repaid the first time a
/// capture would otherwise have been undecodable.
///
/// @post the bytes are ready to be written at offset zero of a new file
[[nodiscard]] std::vector<std::byte> build_log_file_header(std::span<const FormatEntry> formats);

/// One format string recovered from a log file header.
struct DecodedFormat {
  std::uint64_t id = 0;
  std::uint32_t line = 0;
  std::string_view format;
  std::string_view file;
};

/// Reads the header at the start of `content`.
///
/// @pre    `content` outlives the returned views, which point into it
/// @post   `consumed` receives how many bytes the header occupied
/// @errors kTransientIntegrityCheckFailed when the magic or a length is wrong,
///         kInternalBufferTooSmall when the file ends inside the header
[[nodiscard]] core::expected<std::vector<DecodedFormat>>
parse_log_file_header(std::span<const std::byte> content, std::size_t &consumed);

} // namespace volt::log
