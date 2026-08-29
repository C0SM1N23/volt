#pragma once

#include "sim_world.hpp"

#include "volt/pal/timer.hpp"

#include <cstdint>

namespace volt::pal::sim {

/// A timer on the virtual clock.
///
/// Waiting moves time to the expiry instead of blocking, which is what makes a
/// simulated hour finish in a second. A waiter is therefore never late, so a
/// wait always reports exactly one expiration; overrun behaviour belongs to a
/// scenario that models a slow task, not to the clock.
class SimTimer final : public ITimer {
public:
  /// @pre `world` outlives this timer
  explicit SimTimer(detail::SimWorld &world) noexcept : world_{&world} {}

  [[nodiscard]] core::expected<void> arm_once(core::Duration delay) noexcept override;
  [[nodiscard]] core::expected<void> arm_periodic(core::Duration period) noexcept override;
  [[nodiscard]] core::expected<void> disarm() noexcept override;
  [[nodiscard]] core::expected<std::uint64_t> wait() noexcept override;

private:
  detail::SimWorld *world_;
  std::int64_t next_expiry_ns_ = 0;
  std::int64_t period_ns_ = 0;
  bool armed_ = false;
};

} // namespace volt::pal::sim
