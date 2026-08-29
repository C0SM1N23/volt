#pragma once

#include "volt/core/error.hpp"
#include "volt/trace/cycle_clock.hpp"
#include "volt/trace/trace_capture.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace volt::trace {

/// Bytes that begin every capture file.
inline constexpr std::string_view kTraceFileMagic = "VOLTTRC1";

/// A capture together with the calibration that gives it a time axis.
struct StoredCapture {
  Capture capture;
  CycleClock cycle_clock = CycleClock::uncalibrated();
};

/// Serialises a capture, calibration included.
///
/// The raw counter readings are kept rather than nanoseconds, so a capture can
/// be re-examined with a better calibration later, and the measurement that
/// produced the axis stays visible next to the data it scales.
[[nodiscard]] std::vector<std::byte> write_capture(const Capture &capture,
                                                   const CycleClock &cycle_clock);

/// Reads back what `write_capture` produced.
///
/// @pre    `content` outlives nothing; every field is copied out
/// @errors kTransientIntegrityCheckFailed when the magic is wrong,
///         kInternalBufferTooSmall when the file ends early
[[nodiscard]] core::expected<StoredCapture> read_capture(std::span<const std::byte> content);

} // namespace volt::trace
