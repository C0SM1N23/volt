#pragma once

#include "volt/core/strong_id.hpp"

#include <cstdint>

namespace volt::memory {

namespace detail {
struct PoolIndexTag;
}

/// Identifies a pool slot without embedding a process-local address.
using PoolIndex = core::StrongId<detail::PoolIndexTag, std::uint32_t>;

} // namespace volt::memory
