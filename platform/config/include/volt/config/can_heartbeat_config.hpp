#pragma once

#include "volt/config/can_frame_id.hpp"

#include "volt/core/duration.hpp"

#include <cstdint>

namespace volt::config {

/// Configures the CAN heartbeat used for independent node-failure detection.
struct CanHeartbeatConfig final {
  CanFrameId frame_id;
  volt::core::Duration period;
  std::uint16_t miss_threshold = 0;

  [[nodiscard]] bool operator==(const CanHeartbeatConfig &) const noexcept = default;
};

} // namespace volt::config
