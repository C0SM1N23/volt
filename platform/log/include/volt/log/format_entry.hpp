#pragma once

#include "volt/core/hash.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace volt::log {

/// One format string, described where it is written and collected at link time.
///
/// A record on the wire carries only `id`, so the caller never formats and the
/// log never stores the same text twice. The decoder turns an id back into
/// text through this table.
struct FormatEntry {
  /// Hash of the format string, computed while compiling.
  std::uint64_t id;
  const char *format;
  const char *file;
  std::uint32_t line;
};

/// Returns the identifier a format string is logged under.
///
/// `consteval` so it can only ever be computed while compiling: an id worked
/// out at run time would mean the caller path paid for hashing a literal that
/// never changes.
[[nodiscard]] consteval std::uint64_t format_id(std::string_view format) noexcept {
  return core::xxhash64(format);
}

/// Returns every format string linked into this program.
///
/// The entries are placed in a dedicated section, which the linker gathers
/// into one contiguous range and brackets with the two symbols used below. No
/// registration runs at startup, and a format string that is never logged
/// costs nothing but the bytes.
[[nodiscard]] std::span<const FormatEntry> registered_formats() noexcept;

} // namespace volt::log

/// Name of the section the entries are collected in.
///
/// A plain identifier, because the linker only provides `__start_`/`__stop_`
/// symbols for section names that are valid C identifiers.
#define VOLT_LOG_FORMAT_SECTION "volt_log_formats"

/// Places one FormatEntry in the collected section and names it `entry_name`.
///
/// Written as a macro because the entry has to be defined at the point the
/// format string is written, which is what keeps the text, the file and the
/// line together with the call they describe.
#define VOLT_LOG_DEFINE_FORMAT(entry_name, format_string)                                          \
  [[gnu::section(VOLT_LOG_FORMAT_SECTION),                                                         \
    gnu::used]] static constexpr ::volt::log::FormatEntry entry_name {                             \
    ::volt::log::format_id(format_string), (format_string), __FILE__,                              \
        static_cast<std::uint32_t>(__LINE__)                                                       \
  }
