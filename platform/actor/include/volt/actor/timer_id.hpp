#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt {
namespace actor::detail {
struct TimerIdTag;
} // namespace actor::detail

/// Identifies one scheduled actor timer; zero means scheduling failed.
using TimerId = core::StrongId<actor::detail::TimerIdTag, std::uint64_t>;

} // namespace volt
