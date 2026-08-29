#pragma once

#include <cstdint>
#include <string_view>

namespace volt::log {

/// Severity of a log record, ordered so a filter is one comparison.
///
/// The order is the filter: a module configured at kWarn emits kWarn and
/// everything above it. Values are written down because they travel into the
/// binary log, where a renumbering would silently reinterpret old captures.
enum class Level : std::uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kFatal = 5,
};

/// Nothing is emitted at this setting, which is how a module is silenced
/// without a separate flag.
inline constexpr Level kSilent = static_cast<Level>(6);

/// Returns the name a decoder prints for `level`.
[[nodiscard]] constexpr std::string_view to_string(Level level) noexcept {
  switch (level) {
  case Level::kTrace:
    return "TRACE";
  case Level::kDebug:
    return "DEBUG";
  case Level::kInfo:
    return "INFO";
  case Level::kWarn:
    return "WARN";
  case Level::kError:
    return "ERROR";
  case Level::kFatal:
    return "FATAL";
  }
  return "UNKNOWN";
}

} // namespace volt::log
