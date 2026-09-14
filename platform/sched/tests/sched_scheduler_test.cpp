#include "volt/sched/rate_monotonic_scheduler.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <print>
#include <vector>

namespace volt::sched {
namespace {

[[nodiscard]] TaskSpec periodic(std::uint32_t identifier, std::string name, std::int64_t period_us,
                                std::int64_t deadline_us, std::int64_t budget_us) {
  TaskSpec spec;
  spec.id = TaskId{identifier};
  spec.name = std::move(name);
  spec.period = core::Duration::from_us(period_us);
  spec.deadline = core::Duration::from_us(deadline_us);
  spec.wcet_budget = core::Duration::from_us(budget_us);
  return spec;
}

/// Burns pure CPU time until the calling thread has spent `budget`.
///
/// CPU time, not wall time: on a loaded runner a wall-timed burn could cost
/// no cycles at all, and the overrun detector reads the CPU clock.
void burn_thread_cpu(pal::IClock &clock, core::Duration budget) {
  const std::int64_t until = clock.thread_cpu().ns_since_epoch() + budget.ns();
  volatile std::uint64_t sink = 0;
  while (clock.thread_cpu().ns_since_epoch() < until) {
    sink = sink + 1;
  }
}

TEST(SchedulerTest, TasksRunAtTheirOwnCadence) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  std::atomic<std::uint64_t> fast_runs{0};
  std::atomic<std::uint64_t> slow_runs{0};
  ASSERT_TRUE(scheduler
                  .add(periodic(1, "volt-fast", 2'000, 2'000, 1'000),
                       [&fast_runs] { fast_runs.fetch_add(1); })
                  .has_value());
  ASSERT_TRUE(scheduler
                  .add(periodic(2, "volt-slow", 10'000, 10'000, 5'000),
                       [&slow_runs] { slow_runs.fetch_add(1); })
                  .has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  std::print("realtime degraded: {}\n", scheduler.realtime_degraded());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(500)).has_value());
  scheduler.stop();

  // The 2 ms task must have run roughly five times as often as the 10 ms
  // one; the bounds are wide because a shared runner owes us nothing.
  EXPECT_GT(fast_runs.load(), 150U);
  EXPECT_GT(slow_runs.load(), 30U);
  EXPECT_GT(fast_runs.load(), slow_runs.load() * 3);

  // The rate-monotonic order: the faster task holds the higher priority.
  const std::span<const PriorityAssignment> table = scheduler.priorities();
  ASSERT_EQ(table.size(), 2U);
  core::Priority fast_priority{0};
  core::Priority slow_priority{0};
  for (const PriorityAssignment &entry : table) {
    (entry.task == TaskId{1} ? fast_priority : slow_priority) = entry.priority;
  }
  EXPECT_GT(fast_priority.value(), slow_priority.value());
}

TEST(SchedulerTest, ADeadlineMissIsCountedAndVisible) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  // Deadline 1 ms, job sleeps 3 ms: every completed cycle misses.
  const TaskSpec spec = periodic(9, "volt-late", 10'000, 1'000, 5'000);
  ASSERT_TRUE(scheduler
                  .add(spec,
                       [&platform] {
                         ASSERT_TRUE(
                             platform.clock().sleep_for(core::Duration::from_ms(3)).has_value());
                       })
                  .has_value());
  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(100)).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(spec.id);
  ASSERT_TRUE(stats.has_value());
  EXPECT_GT(stats->activations, 0U);
  EXPECT_GE(stats->deadline_misses, stats->activations)
      << "every completed cycle slept past its deadline";
  EXPECT_GT(stats->response_p50_ns, 1'000'000U);
}

TEST(SchedulerTest, OverrunPolicyReachesTheHandlerWithTheSpecsAction) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  struct Report {
    TaskId task{0};
    OverrunAction action{};
  };
  std::mutex reports_guard;
  std::vector<Report> reports;
  scheduler.set_overrun_handler(
      [&](TaskId task, OverrunAction action, core::Duration used, core::Duration budget) {
        EXPECT_GT(used.ns(), budget.ns());
        const std::scoped_lock lock{reports_guard};
        reports.push_back(Report{.task = task, .action = action});
      });

  // Budget 200 us, the job burns two milliseconds of CPU: an overrun every
  // cycle, each policy on its own task.
  TaskSpec logging = periodic(11, "volt-ovr-log", 20'000, 20'000, 200);
  logging.criticality = Criticality::kBestEffort;
  logging.on_overrun = OverrunAction::kLog;
  TaskSpec degrade = periodic(12, "volt-ovr-degr", 20'000, 20'000, 200);
  degrade.criticality = Criticality::kHigh;
  degrade.on_overrun = OverrunAction::kDegrade;
  TaskSpec safety = periodic(13, "volt-ovr-safe", 20'000, 20'000, 200);
  safety.criticality = Criticality::kSafetyCritical;
  safety.on_overrun = OverrunAction::kSafeState;

  auto burner = [&platform] { burn_thread_cpu(platform.clock(), core::Duration::from_ms(2)); };
  ASSERT_TRUE(scheduler.add(logging, burner).has_value());
  ASSERT_TRUE(scheduler.add(degrade, burner).has_value());
  ASSERT_TRUE(scheduler.add(safety, burner).has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(200)).has_value());
  scheduler.stop();

  bool saw_log = false;
  bool saw_degrade = false;
  bool saw_safe_state = false;
  {
    const std::scoped_lock lock{reports_guard};
    for (const Report &report : reports) {
      saw_log = saw_log || (report.task == logging.id && report.action == OverrunAction::kLog);
      saw_degrade =
          saw_degrade || (report.task == degrade.id && report.action == OverrunAction::kDegrade);
      saw_safe_state = saw_safe_state ||
                       (report.task == safety.id && report.action == OverrunAction::kSafeState);
    }
  }
  EXPECT_TRUE(saw_log);
  EXPECT_TRUE(saw_degrade);
  EXPECT_TRUE(saw_safe_state);

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(safety.id);
  ASSERT_TRUE(stats.has_value());
  EXPECT_GT(stats->budget_overruns, 0U);
  EXPECT_GT(stats->execution_p50_ns, 200'000U) << "the burn did not register as CPU time";
}

TEST(SchedulerTest, AJobLongerThanThePeriodSkipsAndCountsActivations) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  // Period 2 ms, job 7 ms: every cycle the timer expires several times
  // while the job still runs. Those activations never execute; the
  // instrumentation must show them, not smooth them over.
  const TaskSpec spec = periodic(21, "volt-skipper", 2'000, 2'000, 1'000);
  ASSERT_TRUE(scheduler
                  .add(spec,
                       [&platform] {
                         ASSERT_TRUE(
                             platform.clock().sleep_for(core::Duration::from_ms(7)).has_value());
                       })
                  .has_value());
  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(200)).has_value());
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(spec.id);
  ASSERT_TRUE(stats.has_value());
  EXPECT_GT(stats->activations, 10U);
  EXPECT_GT(stats->skipped_activations, stats->activations)
      << "a 7 ms job on a 2 ms period must skip more than it runs";
  EXPECT_GE(stats->deadline_misses, stats->skipped_activations)
      << "every skipped activation is a missed deadline";
}

TEST(SchedulerTest, ConfigurationMistakesAreRefusedEarly) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  ASSERT_TRUE(scheduler.add(periodic(1, "volt-a", 1'000, 1'000, 500), [] {}).has_value());
  const core::expected<void> duplicate =
      scheduler.add(periodic(1, "volt-b", 2'000, 2'000, 500), [] {});
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), core::ErrorCode::kConfigDuplicateId);

  ASSERT_TRUE(scheduler.start().has_value());
  const core::expected<void> late = scheduler.add(periodic(2, "volt-c", 1'000, 1'000, 500), [] {});
  ASSERT_FALSE(late.has_value());
  EXPECT_EQ(late.error(), core::ErrorCode::kResourceBusy);
  const core::expected<void> again = scheduler.start();
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kResourceBusy);
  scheduler.stop();
  scheduler.stop();

  EXPECT_TRUE(scheduler.stats(TaskId{1}).has_value());
  EXPECT_FALSE(scheduler.stats(TaskId{404}).has_value());
}

} // namespace
} // namespace volt::sched
