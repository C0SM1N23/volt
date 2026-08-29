#pragma once

#include "volt/config/cluster_config.hpp"
#include "volt/config/node_config.hpp"

#include <cstdint>

namespace volt::config {

/// Returns a stable hash over the complete effective node configuration.
/// @thread any, while config is immutable
/// @rt     allocates; not for data plane
[[nodiscard]] std::uint64_t config_hash(const NodeConfig &config);

/// Returns a stable hash over the complete effective cluster configuration.
/// @thread any, while config is immutable
/// @rt     allocates; not for data plane
[[nodiscard]] std::uint64_t config_hash(const ClusterConfig &config);

} // namespace volt::config
