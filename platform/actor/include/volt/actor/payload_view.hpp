#pragma once

#include <cstddef>
#include <span>

namespace volt {

/// Views immutable payload bytes owned outside the actor runtime.
using PayloadView = std::span<const std::byte>;

} // namespace volt
