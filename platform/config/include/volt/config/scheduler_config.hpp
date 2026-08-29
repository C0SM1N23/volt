#pragma once

#include "volt/config/scheduler_policy.hpp"

#include "volt/core/types.hpp"

namespace volt::config {

/// Configures explicit real-time scheduling for a service.
struct SchedulerConfig final {
  SchedulerPolicy policy = SchedulerPolicy::kFifo;
  volt::core::Priority priority;

  [[nodiscard]] bool operator==(const SchedulerConfig &) const noexcept = default;
};

} // namespace volt::config
