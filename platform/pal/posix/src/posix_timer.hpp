#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/timer.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace volt::pal::posix {

/// A timerfd on CLOCK_MONOTONIC, paired with an eventfd that lets a disarm
/// reach a wait already blocked in the kernel.
///
/// A file descriptor rather than a signal because a signal-based timer cannot
/// be waited on together with sockets, and because signal delivery picks an
/// arbitrary thread, which would break the single-owner rule of SPEC 6.1.
///
/// The cancel channel exists because a supervisor stops a periodic task by
/// disarming its timer from another thread; without it, a wait blocked in
/// read() would sleep forever on a timer that will never fire again.
class PosixTimer final : public ITimer {
public:
  /// Adopts an already created timerfd and cancel eventfd. Only the platform
  /// calls this.
  PosixTimer(detail::FileDescriptor descriptor, detail::FileDescriptor cancel) noexcept
      : descriptor_{std::move(descriptor)}, cancel_{std::move(cancel)} {}

  [[nodiscard]] core::expected<void> arm_once(core::Duration delay) noexcept override;
  [[nodiscard]] core::expected<void> arm_periodic(core::Duration period) noexcept override;
  [[nodiscard]] core::expected<void> disarm() noexcept override;
  [[nodiscard]] core::expected<std::uint64_t> wait() noexcept override;

private:
  enum class Ready : std::uint8_t;

  [[nodiscard]] core::expected<void> arm(core::Duration first, core::Duration repeat) noexcept;

  /// Blocks until the timer expires, the cancel channel speaks, or the wait
  /// has to be retried.
  [[nodiscard]] Ready poll_once() noexcept;

  void drain_cancel() noexcept;

  detail::FileDescriptor descriptor_;
  detail::FileDescriptor cancel_;
  /// Read by the waiting thread, written by whoever arms and disarms; the
  /// cancel token carries the actual wake-up, this only answers the
  /// wait-before-arm case.
  std::atomic<bool> armed_{false};
};

} // namespace volt::pal::posix
