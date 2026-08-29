#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt::config {

namespace detail {
struct NetworkPortTag;
}

/// Identifies a TCP or UDP endpoint port.
using NetworkPort = volt::core::StrongId<detail::NetworkPortTag, std::uint16_t>;

} // namespace volt::config
