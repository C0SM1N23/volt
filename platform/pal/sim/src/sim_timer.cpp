#include "sim_timer.hpp"

namespace volt::pal::sim {

core::expected<void> SimTimer::arm_once(core::Duration delay) noexcept {
  if (delay.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  next_expiry_ns_ = world_->now_ns() + delay.ns();
  period_ns_ = 0;
  armed_ = true;
  world_->record("timer.arm_once", static_cast<std::uint64_t>(next_expiry_ns_));
  return {};
}

core::expected<void> SimTimer::arm_periodic(core::Duration period) noexcept {
  if (period.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  next_expiry_ns_ = world_->now_ns() + period.ns();
  period_ns_ = period.ns();
  armed_ = true;
  world_->record("timer.arm_periodic", static_cast<std::uint64_t>(period_ns_));
  return {};
}

core::expected<void> SimTimer::disarm() noexcept {
  armed_ = false;
  period_ns_ = 0;
  world_->record("timer.disarm", 0);
  return {};
}

core::expected<std::uint64_t> SimTimer::wait() noexcept {
  if (!armed_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  world_->advance_to(next_expiry_ns_);
  world_->record("timer.fire", static_cast<std::uint64_t>(next_expiry_ns_));

  if (period_ns_ > 0) {
    next_expiry_ns_ += period_ns_;
  } else {
    armed_ = false;
  }
  // Exactly one: the waiter arrived the instant the timer fired, because the
  // clock was moved there for it.
  return 1;
}

} // namespace volt::pal::sim
