#include "volt/trace/perfetto_export.hpp"

#include <cstddef>
#include <format>

namespace volt::trace {
namespace {

// The Chrome trace format counts in microseconds, with a fractional part. A
// nanosecond timeline therefore divides by a thousand rather than rounding,
// which is what keeps events a few hundred nanoseconds apart distinguishable.
constexpr double kNanosecondsPerMicrosecond = 1000.0;

// One process, because a capture comes from one program. Correlating several
// nodes arrives with gPTP, and the record already carries the node id it will
// need then.
constexpr int kProcessId = 1;

// Rough size of one rendered event, used to size the output buffer once
// instead of growing it repeatedly. Too small only costs a reallocation.
constexpr std::size_t kEstimatedBytesPerEvent = 128;

/// Escapes what JSON does not allow raw in a string.
///
/// Thread names come from whoever registered them, so they are not assumed to
/// be well behaved.
[[nodiscard]] std::string escaped(std::string_view text) {
  std::string output;
  output.reserve(text.size());
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      output.push_back('\\');
      output.push_back(character);
    } else if (character >= ' ') {
      output.push_back(character);
    } else {
      output += std::format("\\u{:04x}", static_cast<unsigned>(character));
    }
  }
  return output;
}

/// Returns the phase letter the Chrome format uses for a shape.
[[nodiscard]] std::string_view phase_of(EventShape shape) noexcept {
  switch (shape) {
  case EventShape::kIntervalBegin:
    return "B";
  case EventShape::kIntervalEnd:
    return "E";
  case EventShape::kFlowOut:
    return "s";
  case EventShape::kFlowIn:
    return "f";
  case EventShape::kInstant:
    return "i";
  }
  return "i";
}

[[nodiscard]] std::string name_of_thread(const Capture &capture, std::size_t index) {
  if (index < capture.thread_names.size() && !capture.thread_names[index].empty()) {
    return capture.thread_names[index];
  }
  return std::format("thread-{}", index);
}

/// Writes the metadata that gives the process and its threads names.
void append_track_names(std::string &output, const Capture &capture) {
  output += std::format(R"({{"name":"process_name","ph":"M","pid":{},"tid":0,)"
                        R"("args":{{"name":"{}"}}}})",
                        kProcessId, escaped(kDefaultProcessName));
  for (std::size_t index = 0; index < capture.thread_names.size(); ++index) {
    output += std::format(",\n  "
                          R"({{"name":"thread_name","ph":"M","pid":{},"tid":{},)"
                          R"("args":{{"name":"{}"}}}})",
                          kProcessId, index, escaped(name_of_thread(capture, index)));
  }
}

/// Writes one traced event.
void append_event(std::string &output, const TraceRecord &record, const CycleClock &cycle_clock) {
  const EventShape shape = shape_of(record.event);
  const double microseconds =
      static_cast<double>(cycle_clock.to_nanoseconds(record.cycles)) / kNanosecondsPerMicrosecond;

  output += std::format(",\n  "
                        R"({{"name":"{}","cat":"volt","ph":"{}","ts":{:.3f},)"
                        R"("pid":{},"tid":{},"args":{{"arg":{},"node":{}}})",
                        to_string(record.event), phase_of(shape), microseconds, kProcessId,
                        record.thread_index, record.argument, record.node_id);

  if (shape == EventShape::kFlowOut || shape == EventShape::kFlowIn) {
    // The flow id is the message id the caller passed, which is what ties a
    // transmit on one thread to the receive on another. `bp` tells the viewer
    // to bind the arrow to the enclosing slice rather than to the next one.
    output += std::format(R"(,"id":{})", record.argument);
    if (shape == EventShape::kFlowIn) {
      output += R"(,"bp":"e")";
    }
  }
  output += "}";
}

} // namespace

std::string to_chrome_trace(const Capture &capture, const CycleClock &cycle_clock,
                            std::string_view process_name) {
  std::string output;
  output.reserve(capture.records.size() * kEstimatedBytesPerEvent);

  output += "{\n";
  output += std::format(R"(  "displayTimeUnit": "ns",)"
                        "\n");
  output += std::format(R"(  "voltDroppedRecords": {},)"
                        "\n",
                        capture.dropped);
  output += std::format(R"(  "voltProcessName": "{}",)"
                        "\n",
                        escaped(process_name));
  output += R"(  "traceEvents": [)";
  output += "\n  ";

  append_track_names(output, capture);
  for (const TraceRecord &record : capture.records) {
    append_event(output, record, cycle_clock);
  }

  output += "\n  ]\n}\n";
  return output;
}

} // namespace volt::trace
