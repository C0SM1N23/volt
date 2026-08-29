#pragma once

#include <cstdint>
#include <span>

namespace volt {

/// Views fixed-width log arguments without formatting or allocation.
using LogArgs = std::span<const std::uint64_t>;

} // namespace volt
