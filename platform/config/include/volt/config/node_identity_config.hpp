#pragma once

#include "volt/config/node_interfaces.hpp"
#include "volt/config/node_name.hpp"
#include "volt/config/node_role.hpp"
#include "volt/config/realtime_config.hpp"

#include <vector>

namespace volt::config {

/// Describes the local node and the resources assigned to it.
struct NodeIdentityConfig final {
  NodeName id;
  std::vector<NodeRole> roles;
  NodeInterfaces interfaces;
  RealtimeConfig realtime;

  [[nodiscard]] bool operator==(const NodeIdentityConfig &) const noexcept = default;
};

} // namespace volt::config
