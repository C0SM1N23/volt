#include "volt/trace/trace_capture.hpp"
#include "volt/trace/tracer.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace volt::trace {
namespace {

// Set by the build when a sanitizer or coverage is on; see
// cmake/Sanitizers.cmake and cmake/Coverage.cmake.
#if defined(VOLT_INSTRUMENTED)
constexpr bool kInstrumented = true;
#else
constexpr bool kInstrumented = false;
#endif

// A control cycle, sized from SPEC 8.1: a safety-critical task runs on a 1 ms
// period with a few hundred microseconds of budget. K12 asks what fraction of
// such a cycle tracing costs, so the workload has to be one — measuring
// against a loop that finishes in nanoseconds would report the cost of a trace
// point relative to nothing, which is not the question.
constexpr int kWorkPerCycle = 100'000;
constexpr int kTracePointsPerCycle = 10;
constexpr int kCyclesPerBatch = 5;
constexpr int kBatches = 20;

// K12 of SPEC 0.2: turning tracing on costs under two percent.
constexpr double kOverheadBudget = 0.02;

/// Four threads, from the real-time thread budget of SPEC 42.2.
constexpr int kProducerCount = 4;

/// Ten million events, so the ring is overrun many times over and the loss
/// path is exercised rather than merely present.
constexpr int kEventsPerProducer = 2'500'000;

/// Stands in for the work a traced cycle does.
[[nodiscard]] std::uint64_t simulated_cycle(std::uint64_t seed) noexcept {
  std::uint64_t value = seed;
  for (int step = 0; step < kWorkPerCycle; ++step) {
    // Cheap, dependent arithmetic: the dependency chain stops the compiler
    // from hoisting the loop out or vectorising it away.
    value = (value * 6364136223846793005ULL) + 1442695040888963407ULL;
  }
  return value;
}

/// Emits what a task activation records: its activation, its interval, and the
/// messages it handled.
void trace_one_cycle(std::uint32_t task) noexcept {
  VOLT_TRACE(TraceEvent::kTaskActivate, task);
  VOLT_TRACE(TraceEvent::kTaskStart, task);
  for (int message = 0; message < kTracePointsPerCycle - 3; ++message) {
    VOLT_TRACE(TraceEvent::kMessageReceive, static_cast<std::uint32_t>(message));
  }
  VOLT_TRACE(TraceEvent::kTaskEnd, task);
}

/// Runs the workload and returns the median nanoseconds per cycle.
[[nodiscard]] std::int64_t measure(pal::IPlatform &platform, TraceState state) {
  Tracer::instance().set_state(state);

  std::vector<std::int64_t> per_cycle_ns;
  per_cycle_ns.reserve(kBatches);
  std::uint64_t sink = 1;

  for (int batch = 0; batch < kBatches; ++batch) {
    const core::Timestamp start = platform.clock().monotonic();
    for (int cycle = 0; cycle < kCyclesPerBatch; ++cycle) {
      trace_one_cycle(static_cast<std::uint32_t>(cycle));
      sink = simulated_cycle(sink);
    }
    const core::Timestamp finish = platform.clock().monotonic();
    per_cycle_ns.push_back(finish.checked_since(start).value().ns() / kCyclesPerBatch);
    static_cast<void>(collect(Tracer::instance()));
  }

  EXPECT_NE(sink, 0U);
  std::ranges::sort(per_cycle_ns);
  return per_cycle_ns[per_cycle_ns.size() / 2];
}

TEST(TraceBenchmarkTest, TurningTracingOnCostsLessThanTwoPercent) {
  if (kInstrumented) {
    // A sanitizer counts every memory access and coverage counts every line,
    // so the difference measured under either is the tooling's, not the
    // code's. The body still compiles everywhere; only the number is
    // meaningless there.
    GTEST_SKIP() << "overhead is not measurable under instrumentation";
  }

  pal::posix::PosixPlatform platform;
  ASSERT_TRUE(Tracer::prepare_current_thread("bench-main"));
  Tracer::instance().calibrate(platform.clock(), core::Duration::from_ms(20));

  // Warm up so neither measurement pays the first-touch cost of the ring.
  static_cast<void>(measure(platform, TraceState::kEnabled));

  const std::int64_t without_tracing = measure(platform, TraceState::kDisabled);
  const std::int64_t with_tracing = measure(platform, TraceState::kEnabled);
  Tracer::instance().set_state(TraceState::kDisabled);

  ASSERT_GT(without_tracing, 0);
  const double overhead =
      static_cast<double>(with_tracing - without_tracing) / static_cast<double>(without_tracing);
  const std::int64_t per_event_ns = (with_tracing - without_tracing) / kTracePointsPerCycle;

  RecordProperty("cycle_ns_without_tracing", static_cast<int>(without_tracing));
  RecordProperty("cycle_ns_with_tracing", static_cast<int>(with_tracing));
  RecordProperty("trace_points_per_cycle", kTracePointsPerCycle);
  RecordProperty("per_event_ns", static_cast<int>(per_event_ns));
  RecordProperty("overhead_per_mille", static_cast<int>(overhead * 1000.0));

  EXPECT_LT(overhead, kOverheadBudget)
      << "off=" << without_tracing << "ns on=" << with_tracing
      << "ns overhead=" << (overhead * 100.0) << "% per_event=" << per_event_ns << "ns";
}

/// Counts producers that have finished, so the collector knows when to stop.
///
/// A counter rather than asking the threads whether they are joinable: a
/// thread stays joinable until somebody joins it, so that question can never
/// become false on its own and a loop waiting on it never ends.
std::atomic<int> finished_producers{0};

/// Emits its share of the events, then reports that it is done.
void produce_events() {
  const bool registered = Tracer::prepare_current_thread("trace-load");
  EXPECT_TRUE(registered);
  for (int index = 0; index < kEventsPerProducer; ++index) {
    VOLT_TRACE(TraceEvent::kMessageTransmit, static_cast<std::uint32_t>(index));
  }
  // Release: everything this thread pushed must be visible to the collector
  // before the count that tells it this producer is done.
  finished_producers.fetch_add(1, std::memory_order_release);
}

TEST(TraceLoadTest, NoEventIsLostWithoutBeingCounted) {
  // The property is conservation, not delivery. Under load a ring is allowed
  // to refuse an event rather than stall the loop it is measuring, but every
  // refused event has to appear in a counter, or a gap in the timeline becomes
  // indistinguishable from a period when nothing happened.
  pal::posix::PosixPlatform platform;
  Tracer::instance().set_state(TraceState::kEnabled);
  finished_producers.store(0, std::memory_order_relaxed);

  std::vector<std::unique_ptr<pal::IThread>> producers;
  for (int index = 0; index < kProducerCount; ++index) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        platform.create_thread(pal::ThreadConfig{.name = "volt-trace-load"}, produce_events);
    ASSERT_TRUE(thread.has_value());
    producers.push_back(std::move(*thread));
  }

  // Collect while they run, so the rings are drained rather than simply
  // overrun; whatever is left is picked up after they finish.
  std::uint64_t collected = 0;
  // Acquire: pairs with the producers' release, so their records are visible
  // here before the loop decides they are done.
  while (finished_producers.load(std::memory_order_acquire) < kProducerCount) {
    collected += collect(Tracer::instance()).records.size();
  }
  for (const std::unique_ptr<pal::IThread> &producer : producers) {
    EXPECT_TRUE(producer->join().has_value());
  }
  const Capture last = collect(Tracer::instance());
  collected += last.records.size();
  Tracer::instance().set_state(TraceState::kDisabled);

  constexpr std::uint64_t kProduced =
      static_cast<std::uint64_t>(kProducerCount) * kEventsPerProducer;
  EXPECT_EQ(collected + last.dropped, kProduced)
      << "collected=" << collected << " dropped=" << last.dropped;
}

} // namespace
} // namespace volt::trace
