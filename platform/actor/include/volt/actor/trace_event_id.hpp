#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt {

namespace actor::detail {
struct TraceEventIdTag;
} // namespace actor::detail

/// Identifies one stable actor trace event without exposing the trace backend.
using TraceEventId = core::StrongId<actor::detail::TraceEventIdTag, std::uint16_t>;

} // namespace volt
