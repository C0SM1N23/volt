// K1: activation jitter of a 1 ms task, P99, plus the response and pure-CPU
// execution distributions of SPEC 9.4. Numbers land in docs/PERFORMANCE.md,
// the pass bound here is deliberately loose because the runner is shared -
// the tuned-kernel run belongs to P19 (AGENTS.md 8.9).

#include "volt/sched/rate_monotonic_scheduler.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <print>

namespace volt::sched {
namespace {

#if defined(VOLT_INSTRUMENTED)
constexpr bool kInstrumented = true;
#else
constexpr bool kInstrumented = false;
#endif

TEST(SchedBenchmarkTest, KilohertzActivationJitter) {
  if (kInstrumented) {
    GTEST_SKIP() << "a sanitizer or coverage build measures the tool, not the scheduler";
  }
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  TaskSpec spec;
  spec.id = TaskId{1};
  spec.name = "volt-k1";
  spec.period = core::Duration::from_ms(1);
  spec.deadline = core::Duration::from_ms(1);
  spec.wcet_budget = core::Duration::from_us(500);
  // A job the size of a small control step, so the response numbers mean
  // something; pure spinning would report the scheduler measuring itself.
  ASSERT_TRUE(scheduler
                  .add(spec,
                       [&platform] {
                         const std::int64_t until =
                             platform.clock().thread_cpu().ns_since_epoch() + 50'000;
                         volatile std::uint64_t sink = 0;
                         while (platform.clock().thread_cpu().ns_since_epoch() < until) {
                           sink = sink + 1;
                         }
                       })
                  .has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_s(5)).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(spec.id);
  ASSERT_TRUE(stats.has_value());
  std::print("K1 activation jitter: P50 {} ns  P99 {} ns  max {} ns  ({} activations, degraded "
             "rt: {})\n",
             stats->jitter_p50_ns, stats->jitter_p99_ns, stats->jitter_max_ns, stats->activations,
             scheduler.realtime_degraded());
  std::print("K1 response: P50 {} ns  P99 {} ns  max {} ns\n", stats->response_p50_ns,
             stats->response_p99_ns, stats->response_max_ns);
  std::print("K1 execution: P50 {} ns  P99 {} ns  max {} ns\n", stats->execution_p50_ns,
             stats->execution_p99_ns, stats->execution_max_ns);
  std::print("K2 sample: {} misses, {} overruns in {} activations\n", stats->deadline_misses,
             stats->budget_overruns, stats->activations);

  EXPECT_GT(stats->activations, 4'000U);
  // The K1 target of 100 us belongs to PREEMPT_RT + isolcpus; a generic
  // kernel with a fair scheduler gets an order of magnitude of slack, and
  // the real number goes in the report either way.
  EXPECT_LT(stats->jitter_p99_ns, 10'000'000U);
}

} // namespace
} // namespace volt::sched
