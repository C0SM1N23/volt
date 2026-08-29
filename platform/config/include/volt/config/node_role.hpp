#pragma once

#include <cstdint>

namespace volt::config {

/// Declares the responsibilities a compute node may assume.
enum class NodeRole : std::uint8_t { kCompute, kSafety };

} // namespace volt::config
