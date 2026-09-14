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
    // A sleeping thread does not execute; without this ledger the virtual
    // CPU clock below would bill every nap as work.
    slept_by_this_thread_ns += delay.ns();
    return {};
  }

  [[nodiscard]] core::Timestamp thread_cpu() const noexcept override {
    // In a cooperative world a thread "executes" whenever time moves on its
    // watch, except while it sleeps. Deterministic, and it preserves the one
    // property callers rely on: waiting is free, working is not.
    return core::Timestamp::from_ns_since_epoch(world_->now_ns() - slept_by_this_thread_ns);
  }

private:
  /// One ledger per host thread: several cooperative sim threads may share
  /// this clock, and one thread's nap must not discount another's work.
  static thread_local inline std::int64_t slept_by_this_thread_ns;

  detail::SimWorld *world_;
};

} // namespace volt::pal::sim
