#pragma once

#include "volt/config/can_heartbeat_config.hpp"
#include "volt/config/node_name.hpp"
#include "volt/config/ptp_config.hpp"
#include "volt/config/raft_config.hpp"
#include "volt/config/swim_config.hpp"

#include <vector>

namespace volt::config {

/// Describes the cluster timing and membership settings shared by all nodes.
struct ClusterConfig final {
  std::vector<NodeName> peers;
  RaftConfig raft;
  SwimConfig swim;
  CanHeartbeatConfig can_heartbeat;
  PtpConfig ptp;

  [[nodiscard]] bool operator==(const ClusterConfig &) const noexcept = default;
};

} // namespace volt::config
