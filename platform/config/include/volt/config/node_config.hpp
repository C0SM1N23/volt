#pragma once

#include "volt/config/cluster_config.hpp"
#include "volt/config/node_identity_config.hpp"
#include "volt/config/service_config.hpp"
#include "volt/config/telemetry_config.hpp"

#include <vector>

namespace volt::config {

/// Holds the complete effective configuration loaded by one compute node.
struct NodeConfig final {
  NodeIdentityConfig node;
  ClusterConfig cluster;
  std::vector<ServiceConfig> services;
  TelemetryConfig telemetry;

  [[nodiscard]] bool operator==(const NodeConfig &) const noexcept = default;
};

} // namespace volt::config
