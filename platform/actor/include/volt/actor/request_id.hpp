#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt {
namespace actor::detail {
struct RequestIdTag;
} // namespace actor::detail

/// Correlates one request with its response across the actor boundary.
using RequestId = core::StrongId<actor::detail::RequestIdTag, std::uint64_t>;

} // namespace volt
