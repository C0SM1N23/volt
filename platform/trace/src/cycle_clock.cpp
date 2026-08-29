#include "volt/trace/cycle_clock.hpp"

#include <algorithm>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace volt::trace {

std::uint64_t read_cycles() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  // One instruction, no ordering barrier. A few cycles of skew either way does
  // not matter on a timeline whose events are hundreds of nanoseconds apart,
  // and a serialising read would cost more than the event being traced.
  return __rdtsc();
#elif defined(__aarch64__)
  // The virtual counter, which is the architectural equivalent and is readable
  // from user space on Linux.
  std::uint64_t counter = 0;
  asm volatile("mrs %0, cntvct_el0" : "=r"(counter));
  return counter;
#else
  // No cycle counter is reachable here. Returning zero makes every event share
  // a timestamp, which an exported trace shows plainly as a flat timeline
  // rather than pretending to a precision the platform cannot give.
  return 0;
#endif
}

CycleClock CycleClock::calibrate(pal::IClock &clock, core::Duration window) noexcept {
  const std::uint64_t start_cycles = read_cycles();
  const std::int64_t start_ns = clock.monotonic().ns_since_epoch();

  const core::expected<void> slept = clock.sleep_for(window);
  static_cast<void>(slept.has_value());

  const std::uint64_t end_cycles = read_cycles();
  const std::int64_t end_ns = clock.monotonic().ns_since_epoch();

  const std::int64_t elapsed_ns = end_ns - start_ns;
  const std::uint64_t elapsed_cycles = end_cycles - start_cycles;
  if (elapsed_ns <= 0 || elapsed_cycles == 0) {
    // Either the platform has no usable counter or the window was too short to
    // measure. One tick per nanosecond keeps the conversion total; the
    // resulting timeline is wrong in scale but not in order.
    return CycleClock{start_cycles, start_ns, 1.0};
  }
  return CycleClock{start_cycles, start_ns,
                    static_cast<double>(elapsed_cycles) / static_cast<double>(elapsed_ns)};
}

std::int64_t CycleClock::to_nanoseconds(std::uint64_t cycles) const noexcept {
  // Readings taken before calibration started would go negative, which no
  // viewer accepts; they are clamped to the origin instead.
  const std::uint64_t since_origin = cycles > origin_cycles_ ? cycles - origin_cycles_ : 0;
  const auto offset_ns =
      static_cast<std::int64_t>(static_cast<double>(since_origin) / ticks_per_nanosecond_);
  return origin_ns_ + offset_ns;
}

} // namespace volt::trace
