#pragma once

#include "volt/core/duration.hpp"

#include <cstdint>

namespace volt::config {

/// Configures bounded SWIM membership probes.
struct SwimConfig final {
  volt::core::Duration period;
  volt::core::Duration timeout;
  std::uint16_t indirect_probe_count = 0;
  volt::core::Duration indirect_timeout;
  volt::core::Duration suspect_timeout;

  [[nodiscard]] bool operator==(const SwimConfig &) const noexcept = default;
};

} // namespace volt::config
