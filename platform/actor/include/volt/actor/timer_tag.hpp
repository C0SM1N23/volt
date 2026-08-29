#pragma once

#include "volt/core/strong_id.hpp"

namespace volt {
namespace actor::detail {
struct TimerTagTag;
} // namespace actor::detail

/// Carries actor-defined timer meaning without exposing scheduler state.
using TimerTag = core::StrongId<actor::detail::TimerTagTag>;

} // namespace volt
