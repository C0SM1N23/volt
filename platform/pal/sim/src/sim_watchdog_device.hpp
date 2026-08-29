#pragma once

#include "sim_world.hpp"

#include "volt/pal/watchdog_device.hpp"

namespace volt::pal::sim {

/// A simulated watchdog.
///
/// It cannot reset anything, so what it models is the part a supervisor has to
/// get right: the timeout the device settled on, which is not always the one
/// that was asked for. The rounding matches the Linux driver interface so a
/// supervisor that derives its petting period from `timeout()` is exercised
/// the same way here as on the target.
class SimWatchdogDevice final : public IWatchdogDevice {
public:
  /// @pre `world` outlives this device
  explicit SimWatchdogDevice(detail::SimWorld &world) noexcept : world_{&world} {}

  [[nodiscard]] core::expected<void> pet() noexcept override;
  [[nodiscard]] core::expected<void> set_timeout(core::Duration requested) noexcept override;
  [[nodiscard]] core::expected<core::Duration> timeout() const noexcept override;
  [[nodiscard]] core::expected<void> disable() noexcept override;

private:
  // The Linux watchdog interface counts in whole seconds, so a request is
  // rounded down and never below one second.
  static constexpr std::int64_t kResolutionNs = core::kNanosPerSecond;

  // What a driver reports before anyone sets a timeout.
  static constexpr std::int64_t kDefaultTimeoutSeconds = 60;

  detail::SimWorld *world_;
  std::int64_t timeout_seconds_ = kDefaultTimeoutSeconds;
  bool enabled_ = true;
};

} // namespace volt::pal::sim
