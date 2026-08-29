#pragma once

#include "duration.hpp"
#include "level.hpp"
#include "log_args.hpp"
#include "method_id.hpp"
#include "payload_view.hpp"
#include "request_id.hpp"
#include "service_id.hpp"
#include "topic_id.hpp"
#include "trace_event_id.hpp"

#include <cstdint>
#include <string_view>

namespace volt::actor {

/// Receives actor side effects without exposing their transport or OS backend.
class EnvironmentSink {
public:
  /// Destroys a sink through its interface.
  virtual ~EnvironmentSink() = default;

  /// Accepts one publication for the configured communication backend.
  /// @pre    `payload` remains alive through the call; the sink copies it if retained
  /// @thread the actor's dispatcher thread
  /// @rt     implementation-defined by the selected backend
  virtual void publish(TopicId topic, PayloadView payload) noexcept = 0;

  /// Accepts one fully identified request for the communication backend.
  /// @pre    `payload` remains alive through the call; the sink copies it if retained
  /// @thread the actor's dispatcher thread
  /// @rt     implementation-defined by the selected backend
  virtual void call(RequestId request, ServiceId service, MethodId method, PayloadView payload,
                    Duration timeout) noexcept = 0;

  /// Accepts one response for the communication backend.
  /// @pre    `payload` remains alive through the call; the sink copies it if retained
  /// @thread the actor's dispatcher thread
  /// @rt     implementation-defined by the selected backend
  virtual void respond(RequestId request, PayloadView payload) noexcept = 0;

  /// Accepts one structured log record without formatting it on the actor path.
  /// @pre    `message` and `args` remain alive through the call; the sink copies them if retained
  /// @thread the actor's dispatcher thread
  /// @rt     implementation-defined by the selected backend
  virtual void log(Level level, std::string_view message, LogArgs args) noexcept = 0;

  /// Accepts one fixed-width trace event.
  /// @thread the actor's dispatcher thread
  /// @rt     implementation-defined by the selected backend
  virtual void trace(TraceEventId event, std::uint64_t arg) noexcept = 0;
};

} // namespace volt::actor
