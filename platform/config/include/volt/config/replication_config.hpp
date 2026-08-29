#pragma once

#include "volt/config/node_name.hpp"
#include "volt/config/replication_mode.hpp"

#include "volt/core/duration.hpp"

#include <optional>
#include <vector>

namespace volt::config {

/// Describes state synchronization when a service has explicit standbys.
struct ReplicationConfig final {
  ReplicationMode mode = ReplicationMode::kNone;
  std::optional<volt::core::Duration> sync_period;
  std::vector<NodeName> standby_nodes;

  [[nodiscard]] bool operator==(const ReplicationConfig &) const noexcept = default;
};

} // namespace volt::config
