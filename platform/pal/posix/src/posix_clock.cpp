#include "volt/pal/posix/posix_clock.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <cerrno>
#include <ctime>

namespace volt::pal::posix {
namespace {

[[nodiscard]] core::Timestamp read_clock(::clockid_t clock_id) noexcept {
  ::timespec value{};
  if (::clock_gettime(clock_id, &value) != 0) {
    // Reading a clock the kernel supports cannot fail for any reason a caller
    // could act on, so a failure here means the invariant is already broken.
    VOLT_ASSERT(false, "clock_gettime failed for a clock the platform advertises");
  }
  return core::Timestamp::from_ns_since_epoch(detail::to_nanoseconds(value));
}

} // namespace

core::Timestamp PosixClock::monotonic() const noexcept { return read_clock(CLOCK_MONOTONIC); }

core::Timestamp PosixClock::realtime() const noexcept { return read_clock(CLOCK_REALTIME); }

core::expected<void> PosixClock::sleep_for(core::Duration delay) noexcept {
  if (delay.ns() < 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }

  ::timespec remaining = detail::to_timespec(delay);
  // A signal shortens the sleep, so the remainder is slept again. Without this
  // loop the postcondition of sleep_for would hold only when no signal arrives.
  while (true) {
    const int result = ::clock_nanosleep(CLOCK_MONOTONIC, 0, &remaining, &remaining);
    if (result == 0) {
      return {};
    }
    if (result != EINTR) {
      return std::unexpected{detail::from_errno(result)};
    }
  }
}

} // namespace volt::pal::posix
