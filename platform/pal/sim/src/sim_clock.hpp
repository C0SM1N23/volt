#pragma once

#include "sim_world.hpp"

#include "volt/pal/clock.hpp"

namespace volt::pal::sim {

/// The virtual clock.
///
/// It reads nothing from the host, not even to start: an origin taken from the
/// real clock would make two runs of the same scenario differ, and a scenario
/// that cannot be repeated cannot be debugged (SPEC 21.1).
///
/// Time only moves when someone waits for it, so a run costs what its events
/// cost rather than what its simulated duration would.
class SimClock final : public IClock {
public:
  /// @pre `world` outlives this clock
  explicit SimClock(detail::SimWorld &world) noexcept : world_{&world} {}

  [[nodiscard]] core::Timestamp monotonic() const noexcept override {
    return core::Timestamp::from_ns_since_epoch(world_->now_ns());
  }

  [[nodiscard]] core::Timestamp realtime() const noexcept override {
    return core::Timestamp::from_ns_since_epoch(world_->realtime_ns());
  }

  [[nodiscard]] core::expected<void> sleep_for(core::Duration delay) noexcept override {
    if (delay.ns() < 0) {
      return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
    }
    world_->advance_by(delay.ns());
    return {};
  }

private:
  detail::SimWorld *world_;
};

} // namespace volt::pal::sim
