#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"

#include <cstdint>

namespace volt::pal {

/// A timer a thread waits on, which is how every periodic task in VOLT gets
/// its tick.
///
/// Expirations are counted rather than dropped: if a waiter is late, `wait()`
/// reports how many periods elapsed, so an overrun is visible instead of
/// silently shortening the next cycle (SPEC 9.5).
class ITimer {
public:
  ITimer() = default;
  virtual ~ITimer() = default;

  // Deleted so one timer is owned in exactly one place; a duplicated handle
  // would let two threads disarm each other's timer.
  ITimer(const ITimer &) = delete;
  ITimer &operator=(const ITimer &) = delete;
  ITimer(ITimer &&) = delete;
  ITimer &operator=(ITimer &&) = delete;

  /// Arms the timer to fire once after `delay`.
  ///
  /// @pre    `delay` is positive
  /// @post   a later `wait()` returns once the delay has passed
  /// @errors kConfigValueOutOfRange when `delay` is zero or negative
  [[nodiscard]] virtual core::expected<void> arm_once(core::Duration delay) noexcept = 0;

  /// Arms the timer to fire every `period`, starting one period from now.
  ///
  /// @pre    `period` is positive
  /// @errors kConfigValueOutOfRange when `period` is zero or negative
  [[nodiscard]] virtual core::expected<void> arm_periodic(core::Duration period) noexcept = 0;

  /// Stops the timer. Arming again restarts it.
  ///
  /// @post   a subsequent `wait()` blocks until the timer is armed again
  [[nodiscard]] virtual core::expected<void> disarm() noexcept = 0;

  /// Blocks until the timer has fired at least once.
  ///
  /// @pre    the timer is armed
  /// @post   the expiration count is consumed
  /// @thread the owning thread
  /// @rt     blocks by design; this is how a periodic task yields
  /// @errors kResourceUnavailable when the timer is not armed
  [[nodiscard]] virtual core::expected<std::uint64_t> wait() noexcept = 0;
};

} // namespace volt::pal
