#include "posix_timer.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <cerrno>
#include <cstring>
#include <sys/timerfd.h>
#include <unistd.h>

namespace volt::pal::posix {

core::expected<void> PosixTimer::arm(core::Duration first, core::Duration repeat) noexcept {
  ::itimerspec schedule{};
  schedule.it_value = detail::to_timespec(first);
  schedule.it_interval = detail::to_timespec(repeat);

  if (::timerfd_settime(descriptor_.get(), 0, &schedule, nullptr) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  armed_ = first.ns() > 0;
  return {};
}

core::expected<void> PosixTimer::arm_once(core::Duration delay) noexcept {
  if (delay.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return arm(delay, core::Duration{});
}

core::expected<void> PosixTimer::arm_periodic(core::Duration period) noexcept {
  if (period.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return arm(period, period);
}

core::expected<void> PosixTimer::disarm() noexcept {
  // An all-zero itimerspec is how timerfd is told to stop.
  return arm(core::Duration{}, core::Duration{});
}

core::expected<std::uint64_t> PosixTimer::wait() noexcept {
  if (!armed_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  std::uint64_t expirations = 0;
  while (true) {
    const ::ssize_t read_bytes = ::read(descriptor_.get(), &expirations, sizeof(expirations));
    if (read_bytes == static_cast<::ssize_t>(sizeof(expirations))) {
      return expirations;
    }
    if (read_bytes < 0 && errno == EINTR) {
      continue;
    }
    if (read_bytes < 0) {
      return std::unexpected{detail::from_errno(errno)};
    }
    // A timerfd read is all-or-nothing by contract, so a short read means the
    // descriptor is not the timer we opened.
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
}

} // namespace volt::pal::posix
