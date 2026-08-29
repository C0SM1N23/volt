#pragma once

#include <cstdint>

namespace volt::config {

/// Selects the state-replication strategy for a service.
enum class ReplicationMode : std::uint8_t { kNone, kActiveStandby };

} // namespace volt::config
