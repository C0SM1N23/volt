#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt::config {

namespace detail {
struct CanFrameIdTag;
}

/// Identifies a standard or extended CAN frame.
using CanFrameId = volt::core::StrongId<detail::CanFrameIdTag, std::uint32_t>;

} // namespace volt::config
