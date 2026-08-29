#pragma once

#include "volt/config/lease_class.hpp"
#include "volt/config/node_name.hpp"

#include "volt/core/duration.hpp"

#include <string>

namespace volt::config {

/// Configures ownership renewal and conservative self-yield timing.
struct LeaseConfig final {
  std::string resource;
  LeaseClass lease_class = LeaseClass::kUnfenced;
  volt::core::Duration duration;
  volt::core::Duration renew_period;
  volt::core::Duration self_yield_period;
  NodeName preauthorized_successor;

  [[nodiscard]] bool operator==(const LeaseConfig &) const noexcept = default;
};

} // namespace volt::config
