#pragma once

#include "volt/trace/trace_record.hpp"
#include "volt/trace/tracer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace volt::trace {

/// Everything collected from the rings, ready to be exported.
///
/// Records from different threads are kept in one list and sorted by time,
/// because that is the order a timeline is read in and the order a viewer
/// needs to match an interval's begin with its end.
struct Capture {
  std::vector<TraceRecord> records;

  /// Names in registry order, so a record's thread index selects one.
  std::vector<std::string> thread_names;

  /// How many records the rings refused. Reported alongside the trace so a gap
  /// in the timeline is never mistaken for a quiet period.
  std::uint64_t dropped = 0;
};

/// Empties every ring and returns what was in them, sorted by time.
///
/// @pre    the traced threads may keep running; whatever they add afterwards
///         belongs to the next capture
/// @thread the collector thread
/// @rt     allocates; never call it from a control loop
[[nodiscard]] Capture collect(Tracer &tracer);

} // namespace volt::trace
