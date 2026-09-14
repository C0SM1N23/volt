// The drift claims of P13, on both time bases. The virtual hour proves the
// arithmetic exact at K2 scale: 3.6 million activations, zero missed, the
// theoretical timeline landing on the hour to the nanosecond. The real
// 10-second run proves the same engine against an actual kernel timer, where
// only the stop moment - never accumulated drift - may move the count by one.

#include "volt/sched/detail/task_loop.hpp"
#include "volt/sched/rate_monotonic_scheduler.hpp"

#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <print>

namespace volt::sched {
namespace {

[[nodiscard]] TaskSpec kilohertz_task() {
  TaskSpec spec;
  spec.id = TaskId{1};
  spec.name = "volt-drift";
  spec.period = core::Duration::from_ms(1);
  spec.deadline = core::Duration::from_ms(1);
  spec.wcet_budget = core::Duration::from_us(500);
  return spec;
}

TEST(SchedDriftTest, AVirtualHourAtOneKilohertzIsExactlyThreePointSixMillion) {
  constexpr std::uint64_t kHourOfActivations = 3'600'000;
  pal::sim::SimPlatform platform{pal::sim::SimConfig{.seed = 0xD21F7ULL}};
  core::expected<std::unique_ptr<pal::ITimer>> timer = platform.create_timer();
  ASSERT_TRUE(timer.has_value());

  const TaskSpec spec = kilohertz_task();
  TaskMonitor monitor;
  std::uint64_t jobs_ran = 0;
  detail::TaskLoop loop{platform.clock(), **timer, spec, [&jobs_ran] { jobs_ran += 1; },
                        monitor,          nullptr};

  const std::int64_t before_ns = platform.clock().monotonic().ns_since_epoch();
  ASSERT_TRUE(loop.begin().has_value());
  for (std::uint64_t activation = 0; activation < kHourOfActivations; ++activation) {
    const core::expected<detail::CycleOutcome> outcome = loop.step();
    ASSERT_TRUE(outcome.has_value());
    ASSERT_EQ(outcome->expirations, 1U) << "the virtual timer skipped at " << activation;
  }
  const std::int64_t elapsed_ns = platform.clock().monotonic().ns_since_epoch() - before_ns;

  // The strong form of the P13 drift claim: +/- 0, not approximately.
  EXPECT_EQ(jobs_ran, kHourOfActivations);
  EXPECT_EQ(monitor.activations(), kHourOfActivations);
  EXPECT_EQ(monitor.deadline_misses(), 0U);
  EXPECT_EQ(elapsed_ns, static_cast<std::int64_t>(kHourOfActivations) * 1'000'000)
      << "the theoretical timeline drifted off the hour";
  const TaskStatsSnapshot stats = monitor.snapshot();
  EXPECT_EQ(stats.jitter_max_ns, 0U) << "a virtual timer is never late";
}

TEST(SchedDriftTest, AVirtualLaggardShowsItsLatenessFromTheOrigin) {
  // A job of 2.5 ms on a 1 ms period falls 1.5 ms further behind every
  // cycle. Jitter measured against the origin-anchored timeline must show
  // that growing lateness; jitter measured against "now" would report a
  // perfectly punctual task forever - which is exactly the bug the absolute
  // origin exists to prevent, so this test pins it.
  pal::sim::SimPlatform platform{pal::sim::SimConfig{.seed = 0x1A66A2DULL}};
  core::expected<std::unique_ptr<pal::ITimer>> timer = platform.create_timer();
  ASSERT_TRUE(timer.has_value());

  const TaskSpec spec = kilohertz_task();
  TaskMonitor monitor;
  detail::TaskLoop loop{
      platform.clock(),
      **timer,
      spec,
      [&platform] {
        ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_us(2'500)).has_value());
      },
      monitor,
      nullptr};
  ASSERT_TRUE(loop.begin().has_value());
  for (int cycle = 0; cycle < 100; ++cycle) {
    ASSERT_TRUE(loop.step().has_value());
  }

  const TaskStatsSnapshot stats = monitor.snapshot();
  EXPECT_GT(stats.jitter_max_ns, core::Duration::from_ms(100).ns())
      << "one hundred cycles of 1.5 ms lag must be visible against the origin";
  EXPECT_EQ(stats.deadline_misses, 100U) << "every cycle finished past its deadline";
}

TEST(SchedDriftTest, TenRealSecondsAtOneKilohertzLoseNothingToDrift) {
  pal::posix::PosixPlatform platform;
  RateMonotonicScheduler scheduler{platform, SchedulerConfig{}};

  const TaskSpec spec = kilohertz_task();
  ASSERT_TRUE(scheduler.add(spec, [] {}).has_value());
  ASSERT_TRUE(scheduler.start().has_value());
  const std::int64_t started_ns = platform.clock().monotonic().ns_since_epoch();
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_s(10)).has_value());
  const std::int64_t slept_ns = platform.clock().monotonic().ns_since_epoch() - started_ns;
  scheduler.stop();

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(spec.id);
  ASSERT_TRUE(stats.has_value());
  const std::uint64_t delivered = stats->activations + stats->skipped_activations;

  // The kernel counts expirations even when the thread is late reading
  // them, so a drifting loop would show up as a count falling behind the
  // wall clock. Only the stop boundary is allowed to move the total, and
  // only by the cycles that fit between sleep-end and join.
  // Two boundaries stay outside the engine's control: the task thread arms
  // its timer a few cycles after start() returns (a sanitizer stretches
  // that gap to dozens of periods), and the stop moment cuts mid-period.
  // The allowance covers exactly those; real drift from a relative sleep
  // grows without bound and blows straight through it.
  constexpr std::uint64_t kStartupAllowance = 100;
  const std::uint64_t expected = static_cast<std::uint64_t>(slept_ns / 1'000'000);
  std::print("drift: {} activations (+{} skipped) across {} expected cycles\n", stats->activations,
             stats->skipped_activations, expected);
  EXPECT_GE(delivered + kStartupAllowance, expected);
  EXPECT_LE(delivered, expected + 2);
}

} // namespace
} // namespace volt::sched
