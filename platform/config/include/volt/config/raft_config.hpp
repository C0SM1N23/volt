#pragma once

#include "volt/core/duration.hpp"

namespace volt::config {

/// Bounds Raft leader election and heartbeat timing.
struct RaftConfig final {
  volt::core::Duration election_timeout_min;
  volt::core::Duration election_timeout_max;
  volt::core::Duration heartbeat_period;

  [[nodiscard]] bool operator==(const RaftConfig &) const noexcept = default;
};

} // namespace volt::config
