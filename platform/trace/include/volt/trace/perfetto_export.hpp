#pragma once

#include "volt/trace/cycle_clock.hpp"
#include "volt/trace/trace_capture.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace volt::trace {

/// Name the exported trace gives the process.
inline constexpr std::string_view kDefaultProcessName = "volt-runtime";

/// Renders a capture as a Chrome trace, which `ui.perfetto.dev` opens directly.
///
/// The JSON form of the Chrome trace rather than Perfetto's protobuf: the
/// viewer reads both, and JSON can be checked by any script and read by a
/// person when something looks wrong. A binary format would make the trace
/// depend on a schema compiler to inspect at all.
///
/// What the output carries:
/// - a process and one named track per thread, so lanes are labelled;
/// - `B`/`E` pairs for intervals, which is how a viewer draws a duration;
/// - flow arrows from each transmit to the matching receive, keyed on the
///   message id in the record's argument, which is what makes a message
///   visible as it crosses threads;
/// - the dropped-record count, so a gap is never read as a quiet period.
///
/// @pre    `capture` was produced by `collect`
/// @post   the result is a complete JSON document
[[nodiscard]] std::string to_chrome_trace(const Capture &capture, const CycleClock &cycle_clock,
                                          std::string_view process_name = kDefaultProcessName);

} // namespace volt::trace
