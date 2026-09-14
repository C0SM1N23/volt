// The two classes P14 adds, exercised on the real backend, plus the phase
// alignment SPEC 38 asks for: a consumer activated a short, known time after
// its producer finished, so the control chain spends its budget on work
// rather than on waiting for the next activation.

#include "volt/sched/edf_scheduler.hpp"
#include "volt/sched/schedule_checker.hpp"
#include "volt/sched/schedule_generator.hpp"
#include "volt/sched/time_triggered_scheduler.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <print>
#include <vector>

namespace volt::sched {
namespace {

constexpr TaskId kBrake{1};
constexpr TaskId kFusion{2};

[[nodiscard]] ScheduledTask scheduled(TaskId identifier, std::string name, std::int64_t period_us,
                                      std::int64_t wcet_us) {
  return ScheduledTask{.id = identifier,
                       .name = std::move(name),
                       .period = core::Duration::from_us(period_us),
                       .wcet = core::Duration::from_us(wcet_us)};
}

/// The producer/consumer pair of SPEC 38, shrunk so a test finishes quickly
/// while keeping the shape: a slow producer, a fast consumer, and the gap
/// that makes the consumer read a fresh result.
[[nodiscard]] ScheduleConstraints chain_constraints() {
  return ScheduleConstraints{.lane_count = 2,
                             .tasks = {scheduled(kBrake, "volt-brake", 1'000, 300),
                                       scheduled(kFusion, "volt-fusion", 5'000, 600)},
                             .precedences = {Precedence{.before = kFusion,
                                                        .after = kBrake,
                                                        .min_gap = core::Duration::from_us(200)}},
                             .exclusions = {}};
}

[[nodiscard]] TaskSpec edf_task(TaskId identifier, std::string name, std::int64_t period_us,
                                std::int64_t budget_us) {
  TaskSpec spec;
  spec.id = identifier;
  spec.name = std::move(name);
  spec.period = core::Duration::from_us(period_us);
  spec.deadline = core::Duration::from_us(period_us);
  spec.wcet_budget = core::Duration::from_us(budget_us);
  spec.klass = SchedClass::kEdf;
  return spec;
}

TEST(TimeTriggeredTest, EveryTaskRunsOncePerSlotPerHyperperiod) {
  pal::posix::PosixPlatform platform;
  const ScheduleConstraints constraints = chain_constraints();
  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());

  TimeTriggeredScheduler scheduler{platform, *table, TimeTriggeredConfig{}};
  std::atomic<std::uint64_t> brake_runs{0};
  std::atomic<std::uint64_t> fusion_runs{0};
  ASSERT_TRUE(scheduler.bind(kBrake, [&brake_runs] { brake_runs.fetch_add(1); }).has_value());
  ASSERT_TRUE(scheduler.bind(kFusion, [&fusion_runs] { fusion_runs.fetch_add(1); }).has_value());

  ASSERT_TRUE(scheduler.start(platform.clock().monotonic()).has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(200)).has_value());
  scheduler.stop();

  // Five hyperperiods of 5 ms fit in 200 ms; the ratio between the two
  // tasks is what the table fixes, and it must survive execution.
  EXPECT_GT(brake_runs.load(), 100U);
  EXPECT_GT(fusion_runs.load(), 20U);
  EXPECT_GT(brake_runs.load(), fusion_runs.load() * 3);

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(kBrake);
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->activations, brake_runs.load());
  std::print("TT jitter: P50 {} ns  P99 {} ns  max {} ns  (degraded rt: {})\n",
             stats->jitter_p50_ns, stats->jitter_p99_ns, stats->jitter_max_ns,
             scheduler.realtime_degraded());
}

TEST(TimeTriggeredTest, ASlotWithNoBodyRefusesToStart) {
  pal::posix::PosixPlatform platform;
  const core::expected<ScheduleTable> table = generate_schedule(chain_constraints());
  ASSERT_TRUE(table.has_value());

  TimeTriggeredScheduler scheduler{platform, *table, TimeTriggeredConfig{}};
  ASSERT_TRUE(scheduler.bind(kBrake, [] {}).has_value());
  // The fusion slots have nobody to run: a table that says something happens
  // at an instant and then does nothing is a configuration mistake.
  const core::expected<void> started = scheduler.start(platform.clock().monotonic());
  ASSERT_FALSE(started.has_value());
  EXPECT_EQ(started.error(), core::ErrorCode::kConfigMissingField);
}

TEST(TimeTriggeredTest, RefusesABodyForATaskTheTableDoesNotSchedule) {
  pal::posix::PosixPlatform platform;
  const core::expected<ScheduleTable> table = generate_schedule(chain_constraints());
  ASSERT_TRUE(table.has_value());

  TimeTriggeredScheduler scheduler{platform, *table, TimeTriggeredConfig{}};
  const core::expected<void> stranger = scheduler.bind(TaskId{77}, [] {});
  ASSERT_FALSE(stranger.has_value());
  EXPECT_EQ(stranger.error(), core::ErrorCode::kConfigInvalidValue);

  ASSERT_TRUE(scheduler.bind(kBrake, [] {}).has_value());
  const core::expected<void> again = scheduler.bind(kBrake, [] {});
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kConfigDuplicateId);
}

TEST(TimeTriggeredTest, ConsumerActivatesAfterItsProducerFinished) {
  // SPEC 38: the result is ready a short, known time before the consumer is
  // activated, so the chain does not spend a whole period waiting. The table
  // guarantees it by construction; this measures what actually happens.
  pal::posix::PosixPlatform platform;
  const ScheduleConstraints constraints = chain_constraints();
  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(check_schedule(constraints, *table).valid());

  // The claim SPEC 38 actually makes is about the table: the producer's
  // window ends exactly the gap before the consumer's next activation, so
  // even a job that uses its whole budget is finished in time. Checked on
  // the table because it is deterministic - the runtime numbers below
  // additionally carry whatever the producer did not spend.
  std::int64_t tightest_table_gap = table->hyperperiod.ns();
  for (const ScheduleSlot &producer : table->slots) {
    if (producer.task != kFusion) {
      continue;
    }
    for (const ScheduleSlot &consumer : table->slots) {
      if (consumer.task != kBrake || consumer.offset.ns() < producer.end().ns()) {
        continue;
      }
      tightest_table_gap = std::min(tightest_table_gap, consumer.offset.ns() - producer.end().ns());
    }
  }
  EXPECT_EQ(tightest_table_gap, core::Duration::from_us(200).ns())
      << "the generator placed the producer at the instant the gap asks for, no earlier";

  TimeTriggeredScheduler scheduler{platform, *table, TimeTriggeredConfig{}};
  pal::IClock &clock = platform.clock();

  std::atomic<std::int64_t> produced_at{0};
  std::vector<std::int64_t> gaps;
  gaps.reserve(1024);
  std::int64_t consumed = 0;

  ASSERT_TRUE(scheduler
                  .bind(kFusion,
                        [&clock, &produced_at] {
                          // Acquire-release around one instant: the consumer
                          // reads the producer's finish time and nothing else.
                          produced_at.store(clock.monotonic().ns_since_epoch(),
                                            std::memory_order_release);
                        })
                  .has_value());
  ASSERT_TRUE(scheduler
                  .bind(kBrake,
                        [&clock, &produced_at, &gaps, &consumed] {
                          const std::int64_t latest = produced_at.load(std::memory_order_acquire);
                          if (latest == 0 || latest == consumed || gaps.size() == gaps.capacity()) {
                            return;
                          }
                          // The first activation after a fresh result: this
                          // is the gap SPEC 38 budgets 200 us for.
                          consumed = latest;
                          gaps.push_back(clock.monotonic().ns_since_epoch() - latest);
                        })
                  .has_value());

  ASSERT_TRUE(scheduler.start(clock.monotonic()).has_value());
  ASSERT_TRUE(clock.sleep_for(core::Duration::from_ms(400)).has_value());
  scheduler.stop();

  ASSERT_GT(gaps.size(), 20U) << "too few producer/consumer pairs to conclude anything";
  std::ranges::sort(gaps);
  const std::int64_t smallest = gaps.front();
  const std::int64_t median = gaps[gaps.size() / 2];
  std::print("SPEC 38 alignment: min {} ns, median {} ns, max {} ns over {} pairs\n", smallest,
             median, gaps.back(), gaps.size());

  // Never a race: the consumer runs strictly after the result exists, on
  // any scheduler.
  EXPECT_GT(smallest, 0);
  // The point of aligning the phases at all: the consumer waits a fraction
  // of its period for a fresh result, not a whole one on top of the
  // producer's, which is the millisecond SPEC 38 says this buys back.
  EXPECT_LT(median, core::Duration::from_us(1'000).ns());

  // The table's 200 us floor survives execution only while the producer
  // actually runs inside its window. That is what the real-time policy
  // buys, and without it the producer can be pushed past its own slot and
  // publish just before the consumer wakes - an honest property of the
  // environment, not a defect in the alignment, so it is asserted only
  // where the guarantee exists rather than left to flicker.
  if (scheduler.realtime_degraded()) {
    GTEST_SKIP() << "no real-time guarantee here: the producer may miss its own window, so the "
                    "runtime floor says nothing. The table property above is the claim.";
  }
  EXPECT_GE(smallest, core::Duration::from_us(200).ns());
}

TEST(EdfTest, RunsEveryTaskAndRecordsItsNumbers) {
  pal::posix::PosixPlatform platform;
  EdfScheduler scheduler{platform, EdfConfig{}};

  std::atomic<std::uint64_t> fast_runs{0};
  std::atomic<std::uint64_t> slow_runs{0};
  ASSERT_TRUE(scheduler
                  .add(edf_task(kBrake, "volt-edf-fast", 2'000, 400),
                       [&fast_runs] { fast_runs.fetch_add(1); })
                  .has_value());
  ASSERT_TRUE(scheduler
                  .add(edf_task(kFusion, "volt-edf-slow", 10'000, 1'000),
                       [&slow_runs] { slow_runs.fetch_add(1); })
                  .has_value());

  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(platform.clock().sleep_for(core::Duration::from_ms(300)).has_value());
  scheduler.stop();

  EXPECT_GT(fast_runs.load(), 50U);
  EXPECT_GT(slow_runs.load(), 10U);

  const core::expected<TaskStatsSnapshot> stats = scheduler.stats(kBrake);
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->activations, fast_runs.load());
  std::print("EDF jitter: P50 {} ns  P99 {} ns  max {} ns  (reservation refused: {})\n",
             stats->jitter_p50_ns, stats->jitter_p99_ns, stats->jitter_max_ns,
             scheduler.realtime_degraded());
}

TEST(EdfTest, RefusesAReservationTheSporadicModelForbids) {
  pal::posix::PosixPlatform platform;
  EdfScheduler scheduler{platform, EdfConfig{}};

  // A deadline past the period would leave two releases of one task pending
  // at once, which a reservation cannot express - even though the
  // fixed-priority class accepts exactly this shape.
  TaskSpec late = edf_task(kBrake, "volt-edf-late", 1'000, 400);
  late.deadline = core::Duration::from_ms(3);
  const core::expected<void> refused = scheduler.add(late, [] {});
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), core::ErrorCode::kConfigValueOutOfRange);

  TaskSpec greedy = edf_task(kFusion, "volt-edf-greedy", 1'000, 400);
  greedy.wcet_budget = core::Duration::from_us(1'200);
  EXPECT_FALSE(scheduler.add(greedy, [] {}).has_value());

  TaskSpec wrong_class = edf_task(kBrake, "volt-edf-class", 1'000, 400);
  wrong_class.klass = SchedClass::kRateMonotonic;
  EXPECT_FALSE(scheduler.add(wrong_class, [] {}).has_value());
}

} // namespace
} // namespace volt::sched
