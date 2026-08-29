#pragma once

#include "volt/pal/clock.hpp"

namespace volt::pal::posix {

/// The Linux clocks, read through the vDSO so a read costs no syscall.
///
/// `monotonic()` reads CLOCK_MONOTONIC rather than CLOCK_MONOTONIC_RAW so that
/// sleeps, timers and elapsed-time measurements all sit on one time base:
/// clock_nanosleep and timerfd cannot use the raw clock, and mixing the two
/// would let a measured interval disagree with the timer that produced it.
class PosixClock final : public IClock {
public:
  [[nodiscard]] core::Timestamp monotonic() const noexcept override;
  [[nodiscard]] core::Timestamp realtime() const noexcept override;
  [[nodiscard]] core::expected<void> sleep_for(core::Duration delay) noexcept override;
};

} // namespace volt::pal::posix
