#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/timer.hpp"

#include <cstdint>
#include <utility>

namespace volt::pal::posix {

/// A timerfd on CLOCK_MONOTONIC.
///
/// A file descriptor rather than a signal because a signal-based timer cannot
/// be waited on together with sockets, and because signal delivery picks an
/// arbitrary thread, which would break the single-owner rule of SPEC 6.1.
class PosixTimer final : public ITimer {
public:
  /// Adopts an already created timerfd. Only the platform calls this.
  explicit PosixTimer(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  [[nodiscard]] core::expected<void> arm_once(core::Duration delay) noexcept override;
  [[nodiscard]] core::expected<void> arm_periodic(core::Duration period) noexcept override;
  [[nodiscard]] core::expected<void> disarm() noexcept override;
  [[nodiscard]] core::expected<std::uint64_t> wait() noexcept override;

private:
  [[nodiscard]] core::expected<void> arm(core::Duration first, core::Duration repeat) noexcept;

  detail::FileDescriptor descriptor_;
  bool armed_ = false;
};

} // namespace volt::pal::posix
