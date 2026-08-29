#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt::config {

namespace detail {
struct CpuIdTag;
}

/// Identifies one logical processor accepted by the affinity configuration.
using CpuId = volt::core::StrongId<detail::CpuIdTag, std::uint16_t>;

} // namespace volt::config
