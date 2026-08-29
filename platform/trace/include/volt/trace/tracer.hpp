#pragma once

#include "volt/core/time.hpp"
#include "volt/pal/clock.hpp"
#include "volt/trace/cycle_clock.hpp"
#include "volt/trace/trace_event.hpp"
#include "volt/trace/trace_record.hpp"
#include "volt/trace/trace_ring.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace volt::trace {

/// Most threads that may be traced at once.
///
/// The thread index is one byte in the record, and the registry the collector
/// walks is a fixed array rather than a growing one, so registering needs no
/// lock and collection is bounded (SPEC 5.5).
inline constexpr std::size_t kMaxTracedThreads = 64;

/// Whether trace points record anything.
///
/// An enum rather than a boolean so a call reads as what it does; a bare
/// `set_enabled(false)` at a call site says nothing (AGENTS.md 2.14).
enum class TraceState : std::uint8_t { kDisabled, kEnabled };

/// The one tracer.
///
/// A singleton for the same reason the logger is one: a trace point has to be
/// reachable from anywhere without being threaded through every signature, and
/// there is only ever one timeline.
class Tracer final {
public:
  /// Returns the process-wide tracer.
  [[nodiscard]] static Tracer &instance() noexcept;

  /// Turns recording on or off.
  ///
  /// The switch is what K12 measures against: the same binary runs with
  /// tracing on and off, and the difference is the overhead.
  void set_state(TraceState state) noexcept;

  /// Reports whether trace points currently record.
  ///
  /// @rt one relaxed load; this is the whole cost of a disabled trace point
  [[nodiscard]] bool enabled() const noexcept {
    return state_.load(std::memory_order_relaxed) != 0;
  }

  /// Measures the cycle counter against `clock` so the timeline has a scale.
  ///
  /// @pre    called during startup, before the traced work begins
  /// @thread the thread setting tracing up
  void calibrate(pal::IClock &clock, core::Duration window) noexcept;

  /// Returns the calibration used to convert a capture.
  [[nodiscard]] const CycleClock &cycle_clock() const noexcept { return cycle_clock_; }

  /// Gives the calling thread a ring and a name, before its first trace point.
  ///
  /// Creating the ring allocates, so a thread with a deadline calls this while
  /// it is starting up rather than meeting the cost inside a control cycle.
  ///
  /// @pre  `name` only has to stay alive for the call; it is copied
  /// @post returns false once more than kMaxTracedThreads have registered
  static bool prepare_current_thread(std::string_view name) noexcept;

  /// Returns the calling thread's index, or a value past the registry when it
  /// has none.
  [[nodiscard]] static std::size_t current_thread_index() noexcept;

  /// Returns the rings the collector walks.
  [[nodiscard]] std::span<TraceRing *const> rings() noexcept;

  /// Returns the name a thread registered under.
  [[nodiscard]] std::string_view thread_name(std::size_t index) const noexcept;

  /// Returns how many records were thrown away across every ring.
  [[nodiscard]] std::uint64_t dropped_records() const noexcept;

  /// Adds a ring for the calling thread and keeps ownership of it.
  ///
  /// The tracer owns every ring for the life of the process: a ring owned by
  /// its thread would be freed while the collector was still reading it.
  ///
  /// @post returns kMaxTracedThreads when the registry is full
  [[nodiscard]] std::size_t create_ring(std::string_view name) noexcept;

private:
  Tracer() = default;

  // Relaxed: read on every trace point from every thread, and a change is
  // allowed to arrive a few events late.
  std::atomic<std::uint8_t> state_{0};

  CycleClock cycle_clock_ = CycleClock::uncalibrated();

  std::array<std::unique_ptr<TraceRing>, kMaxTracedThreads> owned_rings_{};
  std::array<TraceRing *, kMaxTracedThreads> rings_{};
  std::array<std::string, kMaxTracedThreads> thread_names_{};

  // Published with release so a ring is fully built before the count exposes it.
  std::atomic<std::size_t> ring_count_{0};
  std::atomic<std::size_t> next_ring_{0};
};

/// Records one event on the calling thread's ring.
///
/// @pre    the caller has already checked that tracing is on
/// @rt     wait-free, allocation-free, one cycle-counter read
void emit(TraceEvent event, std::uint32_t argument) noexcept;

/// Opens an interval and closes it when it goes out of scope.
///
/// RAII rather than a pair of calls, so an early return cannot leave an
/// interval open and every timeline stays balanced.
class TraceScope final {
public:
  /// @pre `begin_event` and `end_event` are a matching pair
  TraceScope(TraceEvent begin_event, TraceEvent end_event, std::uint32_t argument) noexcept;
  ~TraceScope();

  TraceScope(const TraceScope &) = delete;
  TraceScope &operator=(const TraceScope &) = delete;
  TraceScope(TraceScope &&) = delete;
  TraceScope &operator=(TraceScope &&) = delete;

private:
  TraceEvent end_event_;
  std::uint32_t argument_;
  bool recording_;
};

} // namespace volt::trace

/// Records `event` with `argument` when tracing is on.
///
/// The check comes first so a disabled trace point costs one relaxed load and
/// a predictable branch, which is what keeps K12 within two percent.
#define VOLT_TRACE(event, argument)                                                                \
  do {                                                                                             \
    if (::volt::trace::Tracer::instance().enabled()) {                                             \
      ::volt::trace::emit((event), (argument));                                                    \
    }                                                                                              \
  } while (false)
