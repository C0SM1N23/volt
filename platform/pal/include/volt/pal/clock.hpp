#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"

namespace volt::pal {

/// The two clocks the platform exposes, and the only way to wait on time.
///
/// This is the raw platform clock, not the cluster time base: nothing here is
/// disciplined by gPTP. `platform/time` builds the global clock on top.
class IClock {
public:
  IClock() = default;
  virtual ~IClock() = default;

  IClock(const IClock &) = delete;
  IClock &operator=(const IClock &) = delete;
  IClock(IClock &&) = delete;
  IClock &operator=(IClock &&) = delete;

  /// Returns a point that never moves backwards and never jumps, suitable for
  /// measuring elapsed time.
  ///
  /// @thread any
  /// @rt     allocation-free, no syscall on a platform with a vDSO
  [[nodiscard]] virtual core::Timestamp monotonic() const noexcept = 0;

  /// Returns a point on the wall clock, which an administrator or NTP may step.
  ///
  /// @thread any
  /// @rt     allocation-free; never use it to measure a duration
  [[nodiscard]] virtual core::Timestamp realtime() const noexcept = 0;

  /// Waits until at least `delay` has passed on the monotonic clock.
  ///
  /// @pre    `delay` is not negative
  /// @post   `monotonic()` advanced by at least `delay`
  /// @thread any
  /// @rt     blocks; never call it from the control loop
  /// @errors kConfigValueOutOfRange when `delay` is negative
  [[nodiscard]] virtual core::expected<void> sleep_for(core::Duration delay) noexcept = 0;
};

} // namespace volt::pal
