#include "sim_watchdog_device.hpp"

#include <algorithm>

namespace volt::pal::sim {

core::expected<void> SimWatchdogDevice::pet() noexcept {
  if (!enabled_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  world_->record("watchdog.pet", static_cast<std::uint64_t>(world_->now_ns()));
  return {};
}

core::expected<void> SimWatchdogDevice::set_timeout(core::Duration requested) noexcept {
  if (requested.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  timeout_seconds_ = std::max<std::int64_t>(requested.ns() / kResolutionNs, 1);
  world_->record("watchdog.set_timeout", static_cast<std::uint64_t>(timeout_seconds_));
  return {};
}

core::expected<core::Duration> SimWatchdogDevice::timeout() const noexcept {
  return core::Duration::from_s(timeout_seconds_);
}

core::expected<void> SimWatchdogDevice::disable() noexcept {
  enabled_ = false;
  world_->record("watchdog.disable", 0);
  return {};
}

} // namespace volt::pal::sim
