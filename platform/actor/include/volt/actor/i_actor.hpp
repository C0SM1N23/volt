#pragma once

#include "environment.hpp"
#include "hash.hpp"
#include "message.hpp"
#include "state_reader.hpp"
#include "state_writer.hpp"
#include "timer_id.hpp"
#include "timer_tag.hpp"

namespace volt {

/// Defines deterministic actor lifecycle and state replication hooks.
/// @satisfies REQ-PLT-030
class IActor {
public:
  /// Destroys an actor through its interface.
  virtual ~IActor() = default;

  /// Starts the actor on its owning dispatcher thread.
  /// @thread the actor's dispatcher thread
  /// @rt     actor-defined
  virtual void on_start(Environment &environment) = 0;

  /// Delivers one immutable message.
  /// @pre    `message.payload` remains alive through the call
  /// @thread the actor's dispatcher thread
  /// @rt     actor-defined
  virtual void on_message(const Message &message, Environment &environment) = 0;

  /// Delivers one timer expiration.
  /// @thread the actor's dispatcher thread
  /// @rt     actor-defined
  virtual void on_timer(TimerId timer, TimerTag tag, Environment &environment) = 0;

  /// Stops the actor without allowing failure to escape shutdown.
  /// @thread the actor's dispatcher thread
  /// @rt     actor-defined
  virtual void on_stop(Environment &environment) noexcept = 0;

  /// Serializes the complete deterministic state.
  /// @pre    `writer` has enough caller-owned storage for this actor's schema
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by the actor schema
  virtual void serialize(StateWriter &writer) const = 0;

  /// Replaces the complete deterministic state from one validated record.
  /// @pre    `reader` carries the actor's supported schema version
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by the actor schema
  virtual void deserialize(StateReader &reader) = 0;

  /// Returns a stable digest of the complete deterministic state.
  /// @thread the actor's dispatcher thread
  /// @rt     allocation-free and bounded by the actor schema
  [[nodiscard]] virtual Hash state_hash() const noexcept = 0;
};

} // namespace volt
