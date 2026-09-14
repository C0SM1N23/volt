// RM against EDF against TT, on one task set, one machine, one afternoon.
// The three classes are activated by the same platform timer and carry the
// same instrumentation, so what the numbers differ by is the policy rather
// than three different ways of telling the time. Published in
// docs/PERFORMANCE.md.

#include "volt/sched/edf_scheduler.hpp"
#include "volt/sched/rate_monotonic_scheduler.hpp"
#include "volt/sched/schedule_generator.hpp"
#include "volt/sched/time_triggered_scheduler.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <print>
#include <string_view>

namespace volt::sched {
namespace {

#if defined(VOLT_INSTRUMENTED)
constexpr bool kInstrumented = true;
#else
constexpr bool kInstrumented = false;
#endif

constexpr TaskId kFast{1};
constexpr TaskId kSlow{2};

constexpr std::int64_t kFastPeriodUs = 2'000;
constexpr std::int64_t kFastBudgetUs = 600;
constexpr std::int64_t kSlowPeriodUs = 10'000;
constexpr std::int64_t kSlowBudgetUs = 1'500;
/// Work each job actually does, comfortably inside its budget so the numbers
/// measure activation rather than overrun.
constexpr std::int64_t kFastWorkUs = 200;
constexpr std::int64_t kSlowWorkUs = 600;
constexpr auto kRunFor = core::Duration::from_s(3);

/// Burns pure CPU time, so a loaded machine cannot make a job look cheap.
void burn(pal::IClock &clock, std::int64_t microseconds) {
  const std::int64_t until = clock.thread_cpu().ns_since_epoch() + (microseconds * 1'000);
  volatile std::uint64_t sink = 0;
  while (clock.thread_cpu().ns_since_epoch() < until) {
    sink = sink + 1;
  }
}

[[nodiscard]] TaskSpec spec(TaskId identifier, std::string name, std::int64_t period_us,
                            std::int64_t budget_us, SchedClass klass) {
  TaskSpec out;
  out.id = identifier;
  out.name = std::move(name);
  out.period = core::Duration::from_us(period_us);
  out.deadline = core::Duration::from_us(period_us);
  out.wcet_budget = core::Duration::from_us(budget_us);
  out.klass = klass;
  return out;
}

void report(std::string_view klass, const TaskStatsSnapshot &stats, bool degraded) {
  std::print("P14 {:<18} jitter P50 {:>8} ns  P99 {:>9} ns  max {:>9} ns   "
             "response P99 {:>9} ns   {} activations, {} misses{}\n",
             klass, stats.jitter_p50_ns, stats.jitter_p99_ns, stats.jitter_max_ns,
             stats.response_p99_ns, stats.activations, stats.deadline_misses,
             degraded ? "   [degraded: no real-time guarantee]" : "");
}

class SchedComparisonTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (kInstrumented) {
      GTEST_SKIP() << "a sanitizer or coverage build measures the tool, not the scheduler";
    }
  }

  pal::posix::PosixPlatform platform_;
};

TEST_F(SchedComparisonTest, RateMonotonic) {
  RateMonotonicScheduler scheduler{platform_, SchedulerConfig{}};
  ASSERT_TRUE(scheduler
                  .add(spec(kFast, "volt-rm-fast", kFastPeriodUs, kFastBudgetUs,
                            SchedClass::kRateMonotonic),
                       [this] { burn(platform_.clock(), kFastWorkUs); })
                  .has_value());
  ASSERT_TRUE(scheduler
                  .add(spec(kSlow, "volt-rm-slow", kSlowPeriodUs, kSlowBudgetUs,
                            SchedClass::kRateMonotonic),
                       [this] { burn(platform_.clock(), kSlowWorkUs); })
                  .has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform_.clock().sleep_for(kRunFor).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(kFast);
  ASSERT_TRUE(stats.has_value());
  report("rate-monotonic", *stats, scheduler.realtime_degraded());
  EXPECT_GT(stats->activations, 1'000U);
}

TEST_F(SchedComparisonTest, EarliestDeadlineFirst) {
  EdfScheduler scheduler{platform_, EdfConfig{}};
  ASSERT_TRUE(scheduler
                  .add(spec(kFast, "volt-edf-fast", kFastPeriodUs, kFastBudgetUs, SchedClass::kEdf),
                       [this] { burn(platform_.clock(), kFastWorkUs); })
                  .has_value());
  ASSERT_TRUE(scheduler
                  .add(spec(kSlow, "volt-edf-slow", kSlowPeriodUs, kSlowBudgetUs, SchedClass::kEdf),
                       [this] { burn(platform_.clock(), kSlowWorkUs); })
                  .has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform_.clock().sleep_for(kRunFor).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(kFast);
  ASSERT_TRUE(stats.has_value());
  report("earliest-deadline", *stats, scheduler.realtime_degraded());
  EXPECT_GT(stats->activations, 1'000U);
}

TEST_F(SchedComparisonTest, TimeTriggered) {
  // Two lanes, which is also what makes this a fair comparison: the other
  // two classes give each task its own thread, so the table gets one
  // dispatcher per task rather than one for both. It is required as well -
  // a slot is not preemptible, and a 1.5 ms job never fits in the 1.4 ms a
  // 2 ms task leaves free on a shared lane.
  const ScheduleConstraints constraints{
      .lane_count = 2,
      .tasks = {ScheduledTask{.id = kFast,
                              .name = "volt-tt-fast",
                              .period = core::Duration::from_us(kFastPeriodUs),
                              .wcet = core::Duration::from_us(kFastBudgetUs)},
                ScheduledTask{.id = kSlow,
                              .name = "volt-tt-slow",
                              .period = core::Duration::from_us(kSlowPeriodUs),
                              .wcet = core::Duration::from_us(kSlowBudgetUs)}},
      .precedences = {},
      .exclusions = {}};
  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());

  TimeTriggeredScheduler scheduler{platform_, *table, TimeTriggeredConfig{}};
  ASSERT_TRUE(scheduler.bind(kFast, [this] { burn(platform_.clock(), kFastWorkUs); }).has_value());
  ASSERT_TRUE(scheduler.bind(kSlow, [this] { burn(platform_.clock(), kSlowWorkUs); }).has_value());

  ASSERT_TRUE(scheduler.start(platform_.clock().monotonic()).has_value());
  ASSERT_TRUE(platform_.clock().sleep_for(kRunFor).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(kFast);
  ASSERT_TRUE(stats.has_value());
  report("time-triggered", *stats, scheduler.realtime_degraded());
  EXPECT_GT(stats->activations, 1'000U);
}

} // namespace
} // namespace volt::sched
