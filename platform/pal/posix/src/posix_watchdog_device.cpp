#include "posix_watchdog_device.hpp"

#include "posix_error.hpp"

#include <algorithm>

#include <cerrno>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace volt::pal::posix {
namespace {

// The driver interface counts in whole seconds, so a shorter request cannot be
// honoured and a longer one is rounded by the driver. `timeout()` reports back
// what the driver settled on.
constexpr std::int64_t kDriverResolutionNs = core::kNanosPerSecond;

} // namespace

core::expected<void> PosixWatchdogDevice::pet() noexcept {
  // Any single byte restarts the countdown; 'w' is what the kernel samples use.
  constexpr std::byte kKeepAlive{'w'};
  while (true) {
    const ::ssize_t written = ::write(descriptor_.get(), &kKeepAlive, sizeof(kKeepAlive));
    if (written == static_cast<::ssize_t>(sizeof(kKeepAlive))) {
      return {};
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<void> PosixWatchdogDevice::set_timeout(core::Duration requested) noexcept {
  if (requested.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  // A sub-second request cannot be expressed, and asking for zero seconds
  // would disable the watchdog rather than tighten it. Not const: the ioctl is
  // _IOWR, so the driver writes the value it settled on back into this object.
  int seconds = std::max(static_cast<int>(requested.ns() / kDriverResolutionNs), 1);
  if (::ioctl(descriptor_.get(), WDIOC_SETTIMEOUT, &seconds) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

core::expected<core::Duration> PosixWatchdogDevice::timeout() const noexcept {
  int seconds = 0;
  if (::ioctl(descriptor_.get(), WDIOC_GETTIMEOUT, &seconds) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return core::Duration::from_s(seconds);
}

core::expected<void> PosixWatchdogDevice::disable() noexcept {
  int options = WDIOS_DISABLECARD;
  if (::ioctl(descriptor_.get(), WDIOC_SETOPTIONS, &options) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

} // namespace volt::pal::posix
