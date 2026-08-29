#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt {
namespace actor::detail {
struct StateVersionTag;
} // namespace actor::detail

/// Identifies the binary schema used by one serialized actor state.
using StateVersion = core::StrongId<actor::detail::StateVersionTag, std::uint16_t>;

} // namespace volt
