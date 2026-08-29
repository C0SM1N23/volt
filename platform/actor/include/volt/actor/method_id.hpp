#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt {
namespace actor::detail {
struct MethodIdTag;
} // namespace actor::detail

/// Identifies a SOME/IP method using its 16-bit wire representation.
using MethodId = core::StrongId<actor::detail::MethodIdTag, std::uint16_t>;

} // namespace volt
