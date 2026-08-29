#include "volt/log/format_entry.hpp"

// The linker creates these two symbols for any section whose name is a valid
// C identifier, bracketing everything placed in it. Declaring them is how a
// program reads a table it never had to build at startup.
extern "C" {
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) — deviation: DEV-005
extern const volt::log::FormatEntry __start_volt_log_formats[];
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) — deviation: DEV-005
extern const volt::log::FormatEntry __stop_volt_log_formats[];
}

namespace volt::log {

std::span<const FormatEntry> registered_formats() noexcept {
  const auto count = static_cast<std::size_t>(__stop_volt_log_formats - __start_volt_log_formats);
  return std::span<const FormatEntry>{__start_volt_log_formats, count};
}

} // namespace volt::log
