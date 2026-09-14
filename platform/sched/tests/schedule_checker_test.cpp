// The checker's job is to reject. Every rule it knows gets a table that
// breaks exactly that rule and nothing else, built by hand rather than by
// the generator - a table the generator could produce would only prove the
// two agree, which is the one thing a verifier must not depend on.

#include "volt/sched/schedule_checker.hpp"

#include <gtest/gtest.h>

namespace volt::sched {
namespace {

constexpr TaskId kFast{1};
constexpr TaskId kSlow{2};
constexpr TaskId kStranger{99};

/// Two tasks, 2 ms and 4 ms, one lane: hyperperiod 4 ms, three instances.
[[nodiscard]] ScheduleConstraints two_task_constraints() {
  return ScheduleConstraints{.lane_count = 1,
                             .tasks = {ScheduledTask{.id = kFast,
                                                     .name = "fast",
                                                     .period = core::Duration::from_ms(2),
                                                     .wcet = core::Duration::from_us(500)},
                                       ScheduledTask{.id = kSlow,
                                                     .name = "slow",
                                                     .period = core::Duration::from_ms(4),
                                                     .wcet = core::Duration::from_us(800)}},
                             .precedences = {},
                             .exclusions = {}};
}

/// The table the constraints above are satisfied by.
[[nodiscard]] ScheduleTable valid_table() {
  return ScheduleTable{.hyperperiod = core::Duration::from_ms(4),
                       .lane_count = 1,
                       .slots = {ScheduleSlot{.task = kFast,
                                              .lane = 0,
                                              .offset = core::Duration::from_us(0),
                                              .budget = core::Duration::from_us(500)},
                                 ScheduleSlot{.task = kSlow,
                                              .lane = 0,
                                              .offset = core::Duration::from_us(500),
                                              .budget = core::Duration::from_us(800)},
                                 ScheduleSlot{.task = kFast,
                                              .lane = 0,
                                              .offset = core::Duration::from_ms(2),
                                              .budget = core::Duration::from_us(500)}}};
}

TEST(ScheduleCheckerTest, AcceptsATableThatSatisfiesEveryRule) {
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), valid_table());
  EXPECT_TRUE(verdict.valid()) << to_string(verdict.defect);
}

TEST(ScheduleCheckerTest, RejectsAHyperperiodThatIsNotTheLeastCommonMultiple) {
  ScheduleTable table = valid_table();
  table.hyperperiod = core::Duration::from_ms(8);
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kWrongHyperperiod);
}

TEST(ScheduleCheckerTest, RejectsASlotForATaskNobodyDeclared) {
  ScheduleTable table = valid_table();
  table.slots[1].task = kStranger;
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kUnknownTask);
}

TEST(ScheduleCheckerTest, RejectsATaskThatDoesNotRunOncePerPeriod) {
  ScheduleTable table = valid_table();
  // The 2 ms task now appears once in a 4 ms hyperperiod: one activation of
  // it simply never happens.
  table.slots.pop_back();
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kWrongInstanceCount);
}

TEST(ScheduleCheckerTest, RejectsAnInstanceThatSpillsPastItsPeriod) {
  ScheduleTable table = valid_table();
  // The second instance of the 2 ms task starts at 3.6 ms and would finish
  // at 4.1 ms, past the period it belongs to.
  table.slots[2].offset = core::Duration::from_us(3'600);
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kOutsideReleaseWindow);
}

TEST(ScheduleCheckerTest, RejectsASlotThatReservesTheWrongBudget) {
  ScheduleTable table = valid_table();
  table.slots[0].budget = core::Duration::from_us(400);
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kWrongBudget);
}

TEST(ScheduleCheckerTest, RejectsTwoJobsOnOneLaneAtOnce) {
  ScheduleTable table = valid_table();
  // The slow task now starts while the fast one still owns the lane.
  table.slots[1].offset = core::Duration::from_us(300);
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kLaneOverlap);
}

TEST(ScheduleCheckerTest, RejectsALaneTheTableDoesNotHave) {
  ScheduleTable table = valid_table();
  table.slots[1].lane = 1;
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kLaneOutOfRange);
}

TEST(ScheduleCheckerTest, RejectsTwoExcludedTasksRunningTogether) {
  ScheduleConstraints constraints = two_task_constraints();
  constraints.lane_count = 2;
  constraints.exclusions.push_back(Exclusion{.first = kFast, .second = kSlow});

  ScheduleTable table = valid_table();
  table.lane_count = 2;
  // Different lanes, same instant: legal for the lanes, forbidden by the
  // resource the two share.
  table.slots[1].lane = 1;
  table.slots[1].offset = core::Duration::from_us(0);
  const ScheduleVerdict verdict = check_schedule(constraints, table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kExclusionViolated);
}

TEST(ScheduleCheckerTest, RejectsAConsumerActivatedTooSoonAfterItsProducer) {
  ScheduleConstraints constraints = two_task_constraints();
  // The slow task produces; the fast one must wait 200 us after it finishes.
  constraints.precedences.push_back(
      Precedence{.before = kSlow, .after = kFast, .min_gap = core::Duration::from_us(200)});

  ScheduleTable table = valid_table();
  // Producer ends at 1300 us, so the consumer may not start before 1500 us,
  // this puts its second instance at 2000 us, which is fine, and its first
  // at 1400 us, which is not.
  table.slots[0].offset = core::Duration::from_us(1'400);
  std::swap(table.slots[0], table.slots[1]);
  const ScheduleVerdict verdict = check_schedule(constraints, table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kPrecedenceViolated);
}

TEST(ScheduleCheckerTest, RejectsATableWhoseSlotsAreOutOfOrder) {
  ScheduleTable table = valid_table();
  std::swap(table.slots[0], table.slots[1]);
  const ScheduleVerdict verdict = check_schedule(two_task_constraints(), table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kUnsorted);
}

TEST(ScheduleCheckerTest, RejectsAPrecedenceBrokenAcrossTheHyperperiodBoundary) {
  ScheduleConstraints constraints = two_task_constraints();
  constraints.precedences.push_back(
      Precedence{.before = kFast, .after = kSlow, .min_gap = core::Duration::from_us(200)});

  // The last producer instance of one hyperperiod also precedes the first
  // consumer instance of the next. This producer ends exactly at the
  // hyperperiod, so its 200 us gap wraps onto a consumer sitting at 0 us -
  // the one case a checker that stopped at the boundary would wave through.
  ScheduleTable table{.hyperperiod = core::Duration::from_ms(4),
                      .lane_count = 1,
                      .slots = {ScheduleSlot{.task = kSlow,
                                             .lane = 0,
                                             .offset = core::Duration::from_us(0),
                                             .budget = core::Duration::from_us(800)},
                                ScheduleSlot{.task = kFast,
                                             .lane = 0,
                                             .offset = core::Duration::from_us(1'000),
                                             .budget = core::Duration::from_us(500)},
                                ScheduleSlot{.task = kFast,
                                             .lane = 0,
                                             .offset = core::Duration::from_us(3'500),
                                             .budget = core::Duration::from_us(500)}}};
  const ScheduleVerdict verdict = check_schedule(constraints, table);
  EXPECT_EQ(verdict.defect, ScheduleDefect::kPrecedenceViolated);
}

TEST(ScheduleCheckerTest, RejectsConstraintsThatDescribeNoTableAtAll) {
  ScheduleConstraints empty;
  EXPECT_FALSE(hyperperiod_of(empty).has_value());

  ScheduleConstraints negative = two_task_constraints();
  negative.tasks[0].period = core::Duration::from_ms(0);
  EXPECT_FALSE(hyperperiod_of(negative).has_value());

  ScheduleConstraints greedy = two_task_constraints();
  greedy.tasks[0].wcet = core::Duration::from_ms(3);
  EXPECT_FALSE(hyperperiod_of(greedy).has_value()) << "a job longer than its period";
}

} // namespace
} // namespace volt::sched
