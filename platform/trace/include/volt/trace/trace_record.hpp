#pragma once

#include "volt/trace/trace_event.hpp"

#include <cstddef>
#include <cstdint>

namespace volt::trace {

/// Bytes one record occupies, in a ring and in a file. Fixed by SPEC 8.4 and
/// stated here as the requirement the layout below has to meet, rather than
/// derived from it: the size is the constraint, not the consequence.
inline constexpr std::size_t kTraceRecordBytes = 16;

/// One traced event, sixteen bytes exactly (SPEC 8.4).
///
/// The size is the design constraint: a control loop that emits several events
/// per cycle pays for every byte in cache pressure, and sixteen bytes is one
/// quarter of a cache line, so four events share a line and a burst costs one
/// fetch. Everything in the layout earns its place at that size.
///
/// `node_id` is carried even though nothing sets it yet. Correlating timelines
/// across nodes arrives with gPTP, and a trace recorded before then still has
/// to be readable by the tool built after: leaving the field out now would
/// mean changing the format later and orphaning every capture taken until then.
struct TraceRecord {
  /// Reading of the cycle counter. Converted to nanoseconds at export, using a
  /// calibration measured against the platform clock.
  std::uint64_t cycles = 0;

  TraceEvent event = TraceEvent::kTaskActivate;

  /// Which node produced it. Always zero until gPTP gives nodes a shared time
  /// base worth correlating on.
  std::uint8_t node_id = 0;

  /// Index into the tracer's thread table, not an operating system id: it has
  /// to fit in a byte, and a trace with more than a few dozen threads is not
  /// one anybody reads.
  std::uint8_t thread_index = 0;

  /// What the event is about: a task id, a message id, a state, a fault code.
  /// Thirty-two bits, because the rest of the budget went to the timestamp.
  std::uint32_t argument = 0;
};

static_assert(sizeof(TraceRecord) == kTraceRecordBytes,
              "SPEC 8.4 fixes the trace record at sixteen bytes");

} // namespace volt::trace
