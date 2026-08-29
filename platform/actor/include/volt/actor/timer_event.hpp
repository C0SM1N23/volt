#pragma once

#include "timer_id.hpp"
#include "timer_tag.hpp"
#include "timestamp.hpp"

namespace volt::actor {

/// Carries one scheduled timer expiration into deterministic dispatch.
struct TimerEvent {
  Timestamp deadline{};
  TimerId id{};
  TimerTag tag{};
};

} // namespace volt::actor
