#pragma once

#include "volt/config/network_port.hpp"

namespace volt::config {

/// Configures non-safety telemetry outputs.
struct TelemetryConfig final {
  bool trace_enabled = false;
  NetworkPort metrics_port;
  bool recording_enabled = false;

  [[nodiscard]] bool operator==(const TelemetryConfig &) const noexcept = default;
};

} // namespace volt::config
