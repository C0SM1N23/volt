#include "volt/trace/trace_capture.hpp"

#include <algorithm>

namespace volt::trace {

Capture collect(Tracer &tracer) {
  Capture capture;
  const std::span<TraceRing *const> rings = tracer.rings();

  capture.thread_names.reserve(rings.size());
  for (std::size_t index = 0; index < rings.size(); ++index) {
    capture.thread_names.emplace_back(tracer.thread_name(index));
  }

  for (TraceRing *const ring : rings) {
    if (ring == nullptr) {
      continue;
    }
    TraceRecord record;
    while (ring->pop(record)) {
      capture.records.push_back(record);
    }
  }

  // Stable, so two events a thread recorded in the same cycle keep the order
  // that thread produced them in. On a counter with coarse resolution that
  // happens often, and reordering them would turn a begin/end pair inside out.
  std::ranges::stable_sort(capture.records, {}, &TraceRecord::cycles);

  capture.dropped = tracer.dropped_records();
  return capture;
}

} // namespace volt::trace
