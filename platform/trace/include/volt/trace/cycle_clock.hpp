#pragma once

#include "volt/core/time.hpp"
#include "volt/pal/clock.hpp"

#include <cstdint>

namespace volt::trace {

/// Reads the processor's cycle counter.
///
/// A trace point has to cost near nothing, and reading the platform clock even
/// through a vDSO costs tens of nanoseconds, which is the same order as the
/// work being traced. The cycle counter is a single instruction.
///
/// @rt allocation-free, no syscall, no barrier
[[nodiscard]] std::uint64_t read_cycles() noexcept;

/// Turns cycle counts into nanoseconds.
///
/// The counter has no defined rate, so it is measured against the platform
/// clock rather than assumed: on one machine it ticks at the nominal frequency
/// and on another it does not, and a trace whose axis is wrong is worse than
/// no trace at all.
///
/// Calibration happens once, off the traced path; conversion happens at
/// export, also off it.
class CycleClock final {
public:
  /// Measures the counter against `clock` over `window`.
  ///
  /// @pre    `window` is positive; a longer window measures more precisely
  /// @post   `to_nanoseconds` converts readings taken around this measurement
  /// @thread the thread that sets up tracing, before any trace point runs
  /// @rt     sleeps for `window`; never call it from a control loop
  static CycleClock calibrate(pal::IClock &clock, core::Duration window) noexcept;

  /// Returns a clock that has not been measured against anything.
  ///
  /// Conversion is then the identity, so a capture taken without calibration
  /// still has its events in the right order and an axis that is plainly wrong
  /// rather than plausibly wrong.
  [[nodiscard]] static CycleClock uncalibrated() noexcept { return CycleClock{0, 0, 1.0}; }

  /// Converts a counter reading into nanoseconds on the platform clock.
  [[nodiscard]] std::int64_t to_nanoseconds(std::uint64_t cycles) const noexcept;

  /// Returns how many counter ticks make up a nanosecond.
  [[nodiscard]] double ticks_per_nanosecond() const noexcept { return ticks_per_nanosecond_; }

  /// Returns the counter reading the calibration started from.
  [[nodiscard]] std::uint64_t origin_cycles() const noexcept { return origin_cycles_; }

  /// Returns the platform time the calibration started at.
  [[nodiscard]] std::int64_t origin_ns() const noexcept { return origin_ns_; }

  /// Rebuilds a calibration that was stored alongside a capture.
  ///
  /// A capture holds raw counter readings, so it has to carry the measurement
  /// that turns them into time; without it the file would be a list of numbers
  /// whose scale died with the process that recorded them.
  [[nodiscard]] static CycleClock from_measurement(std::uint64_t origin_cycles,
                                                   std::int64_t origin_ns,
                                                   double ticks_per_nanosecond) noexcept {
    return CycleClock{origin_cycles, origin_ns,
                      ticks_per_nanosecond > 0.0 ? ticks_per_nanosecond : 1.0};
  }

private:
  CycleClock(std::uint64_t origin_cycles, std::int64_t origin_ns,
             double ticks_per_nanosecond) noexcept
      : origin_cycles_{origin_cycles}, origin_ns_{origin_ns},
        ticks_per_nanosecond_{ticks_per_nanosecond} {}

  std::uint64_t origin_cycles_ = 0;
  std::int64_t origin_ns_ = 0;

  // One by default, so an uncalibrated clock reports cycles as nanoseconds
  // rather than dividing by zero. A trace exported without calibration has a
  // wrong axis, which the exporter says out loud.
  double ticks_per_nanosecond_ = 1.0;
};

} // namespace volt::trace
