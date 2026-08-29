#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/watchdog_device.hpp"

#include <utility>

namespace volt::pal::posix {

/// The Linux watchdog character device, usually `/dev/watchdog`.
///
/// Opening the device arms it on most drivers, so the object is only created
/// once a supervisor is ready to pet it. Closing without `disable()` leaves the
/// watchdog running, which is the direction that fails safe.
class PosixWatchdogDevice final : public IWatchdogDevice {
public:
  /// Adopts an open device. Only the platform calls this.
  explicit PosixWatchdogDevice(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  [[nodiscard]] core::expected<void> pet() noexcept override;
  [[nodiscard]] core::expected<void> set_timeout(core::Duration requested) noexcept override;
  [[nodiscard]] core::expected<core::Duration> timeout() const noexcept override;
  [[nodiscard]] core::expected<void> disable() noexcept override;

private:
  detail::FileDescriptor descriptor_;
};

} // namespace volt::pal::posix
