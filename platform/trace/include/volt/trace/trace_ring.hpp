#pragma once

#include "volt/trace/trace_record.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace volt::trace {

/// Records one ring holds. A power of two so the index wraps with a mask.
///
/// At sixteen bytes a record this is a quarter of a megabyte per thread, which
/// holds several seconds of a control loop emitting a handful of events per
/// millisecond. Long enough that a collector running at a leisurely rate never
/// causes a loss, short enough to sit in memory for every thread at once.
inline constexpr std::size_t kRecordsPerRing = 16384;

/// A single-producer, single-consumer queue of trace records.
///
/// One ring per thread rather than per CPU, deliberately. SPEC 8.4 describes
/// the rings as per-CPU, and for the threads that matter the two are the same
/// thing: SPEC 8.1 and 42.2 pin every real-time thread to its own core, so its
/// ring is that core's ring. What a genuinely per-CPU ring would add is the
/// case where a thread migrates between reading which CPU it is on and writing
/// to that CPU's ring, and two threads then write the same slot. User space
/// cannot disable preemption to close that window, so a per-CPU ring here
/// would be a race dressed up as an optimisation.
///
/// @thread `push` from the owning thread only, `pop` from the collector only
class TraceRing final {
public:
  TraceRing();

  // Rule of five because the collector reaches this object through a
  // registered pointer; moving it would leave that pointer dangling.
  ~TraceRing() = default;
  TraceRing(const TraceRing &) = delete;
  TraceRing &operator=(const TraceRing &) = delete;
  TraceRing(TraceRing &&) = delete;
  TraceRing &operator=(TraceRing &&) = delete;

  /// Appends a record, or counts a loss when the ring is full.
  ///
  /// Never blocks and never allocates: this runs inside the control loop it is
  /// measuring, and a trace point that could stall would change the very
  /// timing it exists to report.
  ///
  /// @thread the owning thread only
  /// @rt     wait-free, a handful of instructions
  void push(const TraceRecord &record) noexcept;

  /// Takes the oldest record, reporting whether there was one.
  ///
  /// @post   `record` is only written when this returns true
  /// @thread the collector thread only
  [[nodiscard]] bool pop(TraceRecord &record) noexcept;

  /// Returns how many records this ring has thrown away.
  ///
  /// Nothing is lost silently: a full ring counts what it refused, and the
  /// exporter reports the total (SPEC 42.1).
  [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
  std::vector<TraceRecord> storage_;

  // Written only by the producer. Release on push pairs with the collector's
  // acquire, so the record is visible before the index that exposes it.
  std::atomic<std::size_t> write_index_{0};

  // Written only by the collector. Release on pop pairs with the producer's
  // acquire, so a slot is not overwritten before it has been read.
  std::atomic<std::size_t> read_index_{0};

  // Producer-only increments, collector reads. Relaxed: the count stands on
  // its own and orders nothing else.
  std::atomic<std::uint64_t> dropped_{0};
};

} // namespace volt::trace
