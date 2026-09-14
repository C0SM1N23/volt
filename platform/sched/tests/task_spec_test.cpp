#include "volt/sched/task_spec.hpp"

#include <gtest/gtest.h>

namespace volt::sched {
namespace {

[[nodiscard]] TaskSpec valid_spec() {
  TaskSpec spec;
  spec.id = TaskId{1};
  spec.name = "brake-control";
  spec.period = core::Duration::from_ms(1);
  spec.deadline = core::Duration::from_us(900);
  spec.wcet_budget = core::Duration::from_us(400);
  spec.criticality = Criticality::kSafetyCritical;
  spec.klass = SchedClass::kRateMonotonic;
  spec.on_overrun = OverrunAction::kSafeState;
  return spec;
}

TEST(TaskSpecTest, TheSpecExampleValidates) {
  EXPECT_TRUE(validate_for_rate_monotonic(valid_spec()).has_value());
}

TEST(TaskSpecTest, DeadlineBeyondThePeriodIsAllowed) {
  // SPEC 9.1 explicitly permits it; a pipeline stage may take longer than
  // its arrival interval as long as the analysis accounts for it.
  TaskSpec spec = valid_spec();
  spec.deadline = core::Duration::from_ms(3);
  EXPECT_TRUE(validate_for_rate_monotonic(spec).has_value());
}

TEST(TaskSpecTest, RejectsZeroTimes) {
  for (const auto broken : {0, 1, 2}) {
    TaskSpec spec = valid_spec();
    if (broken == 0) {
      spec.period = core::Duration{};
    } else if (broken == 1) {
      spec.deadline = core::Duration{};
    } else {
      spec.wcet_budget = core::Duration{};
    }
    const core::expected<void> verdict = validate_for_rate_monotonic(spec);
    ASSERT_FALSE(verdict.has_value()) << "field " << broken;
    EXPECT_EQ(verdict.error(), core::ErrorCode::kConfigValueOutOfRange);
  }
}

TEST(TaskSpecTest, RejectsABudgetBeyondThePeriod) {
  TaskSpec spec = valid_spec();
  spec.wcet_budget = core::Duration::from_ms(2);
  const core::expected<void> verdict = validate_for_rate_monotonic(spec);
  ASSERT_FALSE(verdict.has_value());
  EXPECT_EQ(verdict.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TEST(TaskSpecTest, RejectsKillingASafetyCriticalJob) {
  TaskSpec spec = valid_spec();
  spec.on_overrun = OverrunAction::kKillJob;
  const core::expected<void> verdict = validate_for_rate_monotonic(spec);
  ASSERT_FALSE(verdict.has_value());
  EXPECT_EQ(verdict.error(), core::ErrorCode::kConfigInvalidValue);
}

TEST(TaskSpecTest, KillingABestEffortJobIsFine) {
  TaskSpec spec = valid_spec();
  spec.criticality = Criticality::kBestEffort;
  spec.on_overrun = OverrunAction::kKillJob;
  EXPECT_TRUE(validate_for_rate_monotonic(spec).has_value());
}

TEST(TaskSpecTest, RejectsForeignClassesAndAnonymousTasks) {
  TaskSpec wrong_class = valid_spec();
  wrong_class.klass = SchedClass::kTimeTriggered;
  EXPECT_FALSE(validate_for_rate_monotonic(wrong_class).has_value());

  TaskSpec nameless = valid_spec();
  nameless.name.clear();
  EXPECT_FALSE(validate_for_rate_monotonic(nameless).has_value());
}

} // namespace
} // namespace volt::sched
