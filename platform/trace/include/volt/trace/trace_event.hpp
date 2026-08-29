#pragma once

#include <cstdint>
#include <string_view>

namespace volt::trace {

/// Everything VOLT records on its timeline.
///
/// The list is the one in SPEC 8.4. Identifiers are assigned once and never
/// reordered: a trace captured last month is decoded by a tool built today,
/// and a renumbering would silently relabel every event in it. A new event
/// takes the next free number.
///
/// Intervals appear as a pair rather than as one event with a phase field,
/// which is what lets the record stay sixteen bytes and matches how the events
/// are actually emitted: something starts, something else may happen, it ends.
///
/// Sixteen bits although the list would fit in eight: the record format
/// reserves two bytes for the identifier, so that the list can pass 256 events
/// without every capture taken before that becoming unreadable. Narrowing the
/// enum would move every field after it.
// NOLINTNEXTLINE(performance-enum-size) — deviation: DEV-007
enum class TraceEvent : std::uint16_t {
  kTaskActivate = 1,
  kTaskStart = 2,
  kTaskEnd = 3,
  kDeadlineMiss = 4,
  kMessageTransmit = 5,
  kMessageReceive = 6,
  kRpcBegin = 7,
  kRpcEnd = 8,
  kStateChange = 9,
  kFaultRaised = 10,
  kFailoverBegin = 11,
  kFailoverEnd = 12,
  kAllocationViolation = 13,
};

/// Returns the name a trace viewer shows for `event`.
[[nodiscard]] constexpr std::string_view to_string(TraceEvent event) noexcept {
  switch (event) {
  case TraceEvent::kTaskActivate:
    return "TaskActivate";
  case TraceEvent::kTaskStart:
    return "TaskStart";
  case TraceEvent::kTaskEnd:
    return "TaskEnd";
  case TraceEvent::kDeadlineMiss:
    return "DeadlineMiss";
  case TraceEvent::kMessageTransmit:
    return "MessageTransmit";
  case TraceEvent::kMessageReceive:
    return "MessageReceive";
  case TraceEvent::kRpcBegin:
    return "RpcBegin";
  case TraceEvent::kRpcEnd:
    return "RpcEnd";
  case TraceEvent::kStateChange:
    return "StateChange";
  case TraceEvent::kFaultRaised:
    return "FaultRaised";
  case TraceEvent::kFailoverBegin:
    return "FailoverBegin";
  case TraceEvent::kFailoverEnd:
    return "FailoverEnd";
  case TraceEvent::kAllocationViolation:
    return "AllocationViolation";
  }
  return "Unknown";
}

/// How a viewer should draw an event.
enum class EventShape : std::uint8_t {
  /// A moment with no duration.
  kInstant,
  /// Opens an interval that a matching end closes.
  kIntervalBegin,
  /// Closes the interval its matching begin opened.
  kIntervalEnd,
  /// Starts an arrow to wherever the message is received.
  kFlowOut,
  /// Ends the arrow a transmit started.
  kFlowIn,
};

/// Returns how `event` is drawn.
///
/// Kept next to the enum rather than in the exporter so that adding an event
/// forces the question "what shape is it" to be answered in the same place the
/// event is declared.
[[nodiscard]] constexpr EventShape shape_of(TraceEvent event) noexcept {
  switch (event) {
  case TraceEvent::kTaskStart:
  case TraceEvent::kRpcBegin:
  case TraceEvent::kFailoverBegin:
    return EventShape::kIntervalBegin;
  case TraceEvent::kTaskEnd:
  case TraceEvent::kRpcEnd:
  case TraceEvent::kFailoverEnd:
    return EventShape::kIntervalEnd;
  case TraceEvent::kMessageTransmit:
    return EventShape::kFlowOut;
  case TraceEvent::kMessageReceive:
    return EventShape::kFlowIn;
  case TraceEvent::kTaskActivate:
  case TraceEvent::kDeadlineMiss:
  case TraceEvent::kStateChange:
  case TraceEvent::kFaultRaised:
  case TraceEvent::kAllocationViolation:
    return EventShape::kInstant;
  }
  return EventShape::kInstant;
}

} // namespace volt::trace
