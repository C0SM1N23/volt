#pragma once

#include "volt/config/cpu_id.hpp"
#include "volt/config/lease_config.hpp"
#include "volt/config/replication_config.hpp"
#include "volt/config/scheduler_config.hpp"
#include "volt/config/service_criticality.hpp"
#include "volt/config/service_isolation.hpp"

#include "volt/core/duration.hpp"

#include <optional>
#include <string>
#include <vector>

namespace volt::config {

/// Describes one statically configured service instance.
struct ServiceConfig final {
  std::string name;
  ServiceIsolation isolation = ServiceIsolation::kThread;
  ServiceCriticality criticality = ServiceCriticality::kBestEffort;
  volt::core::Duration period;
  std::vector<CpuId> cpu_ids;
  std::optional<SchedulerConfig> scheduler;
  std::optional<LeaseConfig> lease;
  ReplicationConfig replication;

  [[nodiscard]] bool operator==(const ServiceConfig &) const noexcept = default;
};

} // namespace volt::config
