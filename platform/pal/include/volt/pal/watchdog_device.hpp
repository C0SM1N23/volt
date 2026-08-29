#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"

namespace volt::pal {

/// The hardware watchdog, which resets the board when nothing pets it in time.
///
/// This is the last line of the three-level scheme in SPEC 8.6: software
/// supervision above it can be wrong, this one cannot be talked out of firing.
/// Closing the device without disabling it first leaves the watchdog running,
/// which is the safe direction.
class IWatchdogDevice {
public:
  IWatchdogDevice() = default;
  virtual ~IWatchdogDevice() = default;

  // Deleted because the object owns the device; a second owner could disable a
  // watchdog the first one is relying on.
  IWatchdogDevice(const IWatchdogDevice &) = delete;
  IWatchdogDevice &operator=(const IWatchdogDevice &) = delete;
  IWatchdogDevice(IWatchdogDevice &&) = delete;
  IWatchdogDevice &operator=(IWatchdogDevice &&) = delete;

  /// Restarts the countdown.
  ///
  /// @thread the supervising thread only
  /// @rt     one write to the device; never allocates
  /// @errors kResourceUnavailable when the device is no longer usable
  [[nodiscard]] virtual core::expected<void> pet() noexcept = 0;

  /// Sets how long the device tolerates silence.
  ///
  /// The device may round the request; `timeout()` reports what it accepted,
  /// and the supervisor must derive its petting period from that rather than
  /// from what it asked for.
  ///
  /// @pre    `timeout` is positive
  /// @errors kConfigValueOutOfRange when `timeout` is zero or negative
  [[nodiscard]] virtual core::expected<void> set_timeout(core::Duration timeout) noexcept = 0;

  /// Returns the timeout the device is actually enforcing.
  [[nodiscard]] virtual core::expected<core::Duration> timeout() const noexcept = 0;

  /// Stops the watchdog, for an orderly shutdown that is not a fault.
  ///
  /// @post   the device no longer resets the board
  [[nodiscard]] virtual core::expected<void> disable() noexcept = 0;
};

} // namespace volt::pal
