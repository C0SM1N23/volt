#include "posix_timer.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace volt::pal::posix {

void PosixTimer::drain_cancel() noexcept {
  // Nonblocking: either a token is there and one read clears the whole
  // eventfd counter, or EAGAIN says there was nothing to clear. Both are
  // the outcomes this wants, so the result is bound and deliberately left
  // alone - fortified builds refuse to let it be dropped at the call.
  std::uint64_t tokens = 0;
  [[maybe_unused]] const ::ssize_t drained = ::read(cancel_.get(), &tokens, sizeof(tokens));
}

core::expected<void> PosixTimer::arm(core::Duration first, core::Duration repeat) noexcept {
  ::itimerspec schedule{};
  schedule.it_value = detail::to_timespec(first);
  schedule.it_interval = detail::to_timespec(repeat);

  if (::timerfd_settime(descriptor_.get(), 0, &schedule, nullptr) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  // A cancel token left over from an earlier disarm must not wake the wait
  // that follows this arming.
  drain_cancel();
  // Relaxed: the flag is a fast answer for wait-before-arm; every
  // cross-thread wake-up travels through the descriptors, which the kernel
  // orders itself.
  armed_.store(first.ns() > 0, std::memory_order_relaxed);
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
  const core::expected<void> stopped = arm(core::Duration{}, core::Duration{});
  if (!stopped.has_value()) {
    return stopped;
  }
  // The token reaches a wait blocked in the kernel; without it, disarming a
  // timer nobody expects to fire again would leave that thread sleeping
  // forever, which is exactly how a scheduler shutdown would deadlock.
  const std::uint64_t token = 1;
  if (::write(cancel_.get(), &token, sizeof(token)) != static_cast<::ssize_t>(sizeof(token))) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

/// What one poll over the timer and its cancel channel concluded.
enum class PosixTimer::Ready : std::uint8_t { kExpired, kCancelled, kRetry };

PosixTimer::Ready PosixTimer::poll_once() noexcept {
  std::array<::pollfd, 2> waited{{{.fd = descriptor_.get(), .events = POLLIN, .revents = 0},
                                  {.fd = cancel_.get(), .events = POLLIN, .revents = 0}}};
  if (::poll(waited.data(), waited.size(), -1) < 0) {
    // A signal is not an answer; anything else is reported by the read that
    // follows, which meets the same condition.
    return Ready::kRetry;
  }
  // The cancel wins a tie: a disarm concurrent with an expiration means the
  // owner no longer wants activations, so delivering one anyway would hand
  // the caller work its supervisor just revoked.
  if ((waited[1].revents & POLLIN) != 0) {
    return Ready::kCancelled;
  }
  return (waited[0].revents & POLLIN) != 0 ? Ready::kExpired : Ready::kRetry;
}

core::expected<std::uint64_t> PosixTimer::wait() noexcept {
  // Relaxed: an unarmed timer answers immediately; the racy case where a
  // concurrent disarm lands mid-wait is handled by the cancel token below.
  if (!armed_.load(std::memory_order_relaxed)) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  while (true) {
    const Ready ready = poll_once();
    if (ready == Ready::kCancelled) {
      drain_cancel();
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }
    if (ready == Ready::kRetry) {
      continue;
    }

    std::uint64_t expirations = 0;
    const ::ssize_t read_bytes = ::read(descriptor_.get(), &expirations, sizeof(expirations));
    if (read_bytes == static_cast<::ssize_t>(sizeof(expirations))) {
      return expirations;
    }
    if (read_bytes < 0 && (errno == EINTR || errno == EAGAIN)) {
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
