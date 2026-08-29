#pragma once

#include "volt/config/ptp_role.hpp"

#include "volt/core/duration.hpp"

namespace volt::config {

/// Configures global-time synchronization for one cluster member.
struct PtpConfig final {
  PtpRole role = PtpRole::kAuto;
  volt::core::Duration sync_interval;
  volt::core::Duration maximum_offset;

  [[nodiscard]] bool operator==(const PtpConfig &) const noexcept = default;
};

} // namespace volt::config
