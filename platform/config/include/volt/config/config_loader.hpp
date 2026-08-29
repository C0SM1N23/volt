#pragma once

#include "volt/config/cluster_config.hpp"
#include "volt/config/load_report.hpp"
#include "volt/config/node_config.hpp"

#include "volt/core/error.hpp"

#include <string_view>

namespace volt::config {

/// Loads and strictly validates a complete node YAML document.
/// @pre    path remains valid only for the call; report is owned by the caller
/// @post   success means every input field was consumed and represented in the returned value
/// @thread initialization thread
/// @rt     performs file IO and allocates; not for data plane
/// @errors kConfigMissingField, kConfigInvalidValue, kConfigValueOutOfRange,
///         kConfigDuplicateId, or kConfigCyclicDependency when validation fails
[[nodiscard]] volt::expected<NodeConfig> load_node_config(std::string_view path,
                                                          LoadReport &report);

/// Loads and strictly validates a standalone cluster YAML document.
/// @pre    path remains valid only for the call; report is owned by the caller
/// @post   success means every input field was consumed and represented in the returned value
/// @thread initialization thread
/// @rt     performs file IO and allocates; not for data plane
/// @errors kConfigMissingField, kConfigInvalidValue, kConfigValueOutOfRange, or
///         kConfigDuplicateId when validation fails
[[nodiscard]] volt::expected<ClusterConfig> load_cluster_config(std::string_view path,
                                                                LoadReport &report);

} // namespace volt::config
