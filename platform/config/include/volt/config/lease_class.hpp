#pragma once

#include <cstdint>

namespace volt::config {

/// Selects whether an actuator resource enforces fencing tokens.
enum class LeaseClass : std::uint8_t { kFenced, kUnfenced };

} // namespace volt::config
