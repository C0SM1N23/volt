#pragma once

#include "volt/config/cpu_id.hpp"

#include <vector>

namespace volt::config {

/// Holds initialization-time real-time process settings for a node.
struct RealtimeConfig final {
  std::vector<CpuId> isolated_cpu_ids;
  bool lock_memory = false;
  bool reset_scheduler_on_fork = false;

  [[nodiscard]] bool operator==(const RealtimeConfig &) const noexcept = default;
};

} // namespace volt::config
