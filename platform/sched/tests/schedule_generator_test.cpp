#include "volt/sched/schedule_generator.hpp"

#include "volt/sched/schedule_checker.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace volt::sched {
namespace {

constexpr TaskId kBrake{1};
constexpr TaskId kFusion{2};
constexpr TaskId kDynamics{3};
constexpr TaskId kDiagnostics{4};

[[nodiscard]] ScheduledTask task(TaskId identifier, std::string name, std::int64_t period_us,
                                 std::int64_t wcet_us) {
  return ScheduledTask{.id = identifier,
                       .name = std::move(name),
                       .period = core::Duration::from_us(period_us),
                       .wcet = core::Duration::from_us(wcet_us)};
}

/// The control set of SPEC 3.2, with the budgets of the SPEC 9.3 example.
///
/// Two lanes, because a time-triggered slot is not preemptible: the 3.2 ms
/// diagnostics job needs that much uninterrupted lane, and a 1 ms task
/// leaves gaps of 600 us. That is a property of the class, not a limit of
/// the search - see RefusesALongJobThatCannotFitBetweenFastOnes.
[[nodiscard]] ScheduleConstraints vehicle_constraints() {
  return ScheduleConstraints{.lane_count = 2,
                             .tasks = {task(kBrake, "brake-control", 1'000, 400),
                                       task(kFusion, "sensor-fusion", 5'000, 610),
                                       task(kDynamics, "vehicle-dynamics", 10'000, 1'720),
                                       task(kDiagnostics, "diagnostics", 20'000, 3'210)},
                             .precedences = {},
                             .exclusions = {}};
}

[[nodiscard]] std::size_t count_slots(const ScheduleTable &table, TaskId identifier) {
  return static_cast<std::size_t>(std::ranges::count_if(
      table.slots, [identifier](const ScheduleSlot &slot) { return slot.task == identifier; }));
}

TEST(ScheduleGeneratorTest, ProducesATableTheCheckerAccepts) {
  const ScheduleConstraints constraints = vehicle_constraints();
  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());

  const ScheduleVerdict verdict = check_schedule(constraints, *table);
  EXPECT_TRUE(verdict.valid()) << to_string(verdict.defect);
  EXPECT_EQ(table->hyperperiod.ns(), core::Duration::from_us(20'000).ns());
}

TEST(ScheduleGeneratorTest, GivesEveryTaskOneSlotPerPeriod) {
  const core::expected<ScheduleTable> table = generate_schedule(vehicle_constraints());
  ASSERT_TRUE(table.has_value());

  EXPECT_EQ(count_slots(*table, kBrake), 20U);
  EXPECT_EQ(count_slots(*table, kFusion), 4U);
  EXPECT_EQ(count_slots(*table, kDynamics), 2U);
  EXPECT_EQ(count_slots(*table, kDiagnostics), 1U);
}

TEST(ScheduleGeneratorTest, TheSameConstraintsAlwaysGiveTheSameTable) {
  // An offline tool whose output moved between runs would make every
  // downstream diff meaningless and every measurement unrepeatable.
  const core::expected<ScheduleTable> first = generate_schedule(vehicle_constraints());
  const core::expected<ScheduleTable> second = generate_schedule(vehicle_constraints());
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(first->slots.size(), second->slots.size());

  for (std::size_t index = 0; index < first->slots.size(); ++index) {
    EXPECT_EQ(first->slots[index].task, second->slots[index].task) << "slot " << index;
    EXPECT_EQ(first->slots[index].offset.ns(), second->slots[index].offset.ns())
        << "slot " << index;
    EXPECT_EQ(first->slots[index].lane, second->slots[index].lane) << "slot " << index;
  }
}

TEST(ScheduleGeneratorTest, HonoursAPrecedenceGapBetweenProducerAndConsumer) {
  ScheduleConstraints constraints = vehicle_constraints();
  constraints.precedences.push_back(
      Precedence{.before = kFusion, .after = kBrake, .min_gap = core::Duration::from_us(200)});

  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());
  EXPECT_TRUE(check_schedule(constraints, *table).valid());

  // Every brake activation sits at least the gap after every fusion job
  // that precedes it, the boundary included.
  for (const ScheduleSlot &producer : table->slots) {
    if (producer.task != kFusion) {
      continue;
    }
    for (const ScheduleSlot &consumer : table->slots) {
      if (consumer.task != kBrake || consumer.offset.ns() <= producer.offset.ns()) {
        continue;
      }
      EXPECT_GE(consumer.offset.ns(), producer.end().ns() + core::Duration::from_us(200).ns())
          << "brake at " << consumer.offset.ns() << " follows fusion at " << producer.offset.ns();
    }
  }
}

TEST(ScheduleGeneratorTest, KeepsExcludedTasksApartAcrossLanes) {
  // Two tasks with the same period and the same window: with two lanes free
  // and nothing else to say, the earliest placement puts both at offset zero
  // on separate lanes. The exclusion is the only reason they cannot be
  // there, so a generator that ignored it would produce exactly that - which
  // is what makes this pair worth testing rather than a set where the lanes
  // happen to separate them anyway.
  ScheduleConstraints constraints{
      .lane_count = 2,
      .tasks = {task(kFusion, "reads-the-bus", 1'000, 300),
                task(kDiagnostics, "writes-the-bus", 1'000, 300)},
      .precedences = {},
      .exclusions = {Exclusion{.first = kFusion, .second = kDiagnostics}}};

  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());
  EXPECT_TRUE(check_schedule(constraints, *table).valid());
  ASSERT_EQ(table->slots.size(), 2U);

  const ScheduleSlot &first = table->slots[0];
  const ScheduleSlot &second = table->slots[1];
  EXPECT_GE(second.offset.ns(), first.end().ns())
      << "two tasks sharing a device were scheduled at the same instant";
}

TEST(ScheduleGeneratorTest, UsesTheSecondLaneWhenOneWouldNotFit) {
  // Two tasks that each need most of their period: one lane cannot hold
  // both, two can.
  ScheduleConstraints constraints{
      .lane_count = 2,
      .tasks = {task(kBrake, "a", 1'000, 700), task(kFusion, "b", 1'000, 700)},
      .precedences = {},
      .exclusions = {}};
  const core::expected<ScheduleTable> table = generate_schedule(constraints);
  ASSERT_TRUE(table.has_value());
  EXPECT_TRUE(check_schedule(constraints, *table).valid());
  EXPECT_NE(table->slots[0].lane, table->slots[1].lane);
}

TEST(ScheduleGeneratorTest, RefusesALongJobThatCannotFitBetweenFastOnes) {
  // A slot is a contiguous reservation, so a 3.21 ms job wants 3.21 ms of
  // uninterrupted lane. A 1 ms task holding 400 us of every period leaves
  // gaps of 600 us, and no offset makes a gap bigger. The honest answer is
  // "not on one lane", and the fix is another lane or a shorter job - never
  // a cleverer search, which is why this refusal is a test and not a bug.
  ScheduleConstraints tight{.lane_count = 1,
                            .tasks = {task(kBrake, "brake-control", 1'000, 400),
                                      task(kDiagnostics, "diagnostics", 20'000, 3'210)},
                            .precedences = {},
                            .exclusions = {}};
  EXPECT_FALSE(generate_schedule(tight).has_value());

  tight.lane_count = 2;
  const core::expected<ScheduleTable> roomier = generate_schedule(tight);
  ASSERT_TRUE(roomier.has_value());
  EXPECT_TRUE(check_schedule(tight, *roomier).valid());
}

TEST(ScheduleGeneratorTest, RefusesConstraintsNoTableCouldSatisfy) {
  ScheduleConstraints overloaded{
      .lane_count = 1,
      .tasks = {task(kBrake, "a", 1'000, 700), task(kFusion, "b", 1'000, 700)},
      .precedences = {},
      .exclusions = {}};
  EXPECT_FALSE(generate_schedule(overloaded).has_value()) << "140 percent of one lane";

  ScheduleConstraints laneless = vehicle_constraints();
  laneless.lane_count = 0;
  const core::expected<ScheduleTable> refused = generate_schedule(laneless);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TEST(ScheduleGeneratorTest, MutualPrecedenceIsSatisfiedBySeparation) {
  // Two rules pointing both ways are not a contradiction here. The rule
  // forbids a consumer from starting inside its producer's blackout window,
  // so two tasks only have to stay out of each other's, which separation in
  // time achieves. Saying so in a test keeps these semantics from being
  // mistaken for a dependency cycle by whoever reads the generator next.
  ScheduleConstraints mutual = vehicle_constraints();
  mutual.precedences.push_back(
      Precedence{.before = kFusion, .after = kDynamics, .min_gap = core::Duration::from_us(100)});
  mutual.precedences.push_back(
      Precedence{.before = kDynamics, .after = kFusion, .min_gap = core::Duration::from_us(100)});

  const core::expected<ScheduleTable> table = generate_schedule(mutual);
  ASSERT_TRUE(table.has_value());
  EXPECT_TRUE(check_schedule(mutual, *table).valid());
}

TEST(ScheduleGeneratorTest, RefusesAGapNoHyperperiodCouldHold) {
  // A blackout window as long as the hyperperiod leaves the consumer no
  // instant anywhere on the cycle.
  ScheduleConstraints impossible = vehicle_constraints();
  impossible.precedences.push_back(
      Precedence{.before = kFusion, .after = kBrake, .min_gap = core::Duration::from_ms(20)});
  EXPECT_FALSE(generate_schedule(impossible).has_value());
}

} // namespace
} // namespace volt::sched
