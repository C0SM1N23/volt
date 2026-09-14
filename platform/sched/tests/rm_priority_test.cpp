#include "volt/sched/rm_priority.hpp"

#include <gtest/gtest.h>

#include <array>

namespace volt::sched {
namespace {

[[nodiscard]] TaskSpec task(std::uint32_t identifier, std::int64_t period_us,
                            std::int64_t deadline_us = 0) {
  TaskSpec spec;
  spec.id = TaskId{identifier};
  spec.name = "task";
  spec.period = core::Duration::from_us(period_us);
  spec.deadline = core::Duration::from_us(deadline_us == 0 ? period_us : deadline_us);
  spec.wcet_budget = core::Duration::from_us(period_us / 2);
  return spec;
}

[[nodiscard]] core::Priority priority_of(std::span<const PriorityAssignment> table,
                                         std::uint32_t identifier) {
  for (const PriorityAssignment &entry : table) {
    if (entry.task == TaskId{identifier}) {
      return entry.priority;
    }
  }
  return core::Priority{0};
}

TEST(RmPriorityTest, ShorterPeriodOutranksLonger) {
  const std::array<TaskSpec, 3> specs{task(1, 10'000), task(2, 1'000), task(3, 5'000)};
  const core::expected<std::vector<PriorityAssignment>> table =
      assign_rate_monotonic_priorities(specs, core::Priority{10}, core::Priority{79});
  ASSERT_TRUE(table.has_value());

  EXPECT_EQ(priority_of(*table, 2).value(), 79U);
  EXPECT_EQ(priority_of(*table, 3).value(), 78U);
  EXPECT_EQ(priority_of(*table, 1).value(), 77U);
}

TEST(RmPriorityTest, EqualPeriodsBreakTiesByDeadlineThenId) {
  const std::array<TaskSpec, 3> specs{task(7, 1'000, 900), task(3, 1'000, 500),
                                      task(5, 1'000, 900)};
  const core::expected<std::vector<PriorityAssignment>> table =
      assign_rate_monotonic_priorities(specs, core::Priority{10}, core::Priority{79});
  ASSERT_TRUE(table.has_value());

  // Tighter deadline first; identical deadlines fall back to the id so the
  // table is the same on every build.
  EXPECT_EQ(priority_of(*table, 3).value(), 79U);
  EXPECT_EQ(priority_of(*table, 5).value(), 78U);
  EXPECT_EQ(priority_of(*table, 7).value(), 77U);
}

TEST(RmPriorityTest, EveryTaskGetsADistinctPriority) {
  const std::array<TaskSpec, 4> specs{task(1, 1'000), task(2, 1'000), task(3, 1'000),
                                      task(4, 1'000)};
  const core::expected<std::vector<PriorityAssignment>> table =
      assign_rate_monotonic_priorities(specs, core::Priority{10}, core::Priority{79});
  ASSERT_TRUE(table.has_value());
  for (std::size_t left = 0; left < table->size(); ++left) {
    for (std::size_t right = left + 1; right < table->size(); ++right) {
      EXPECT_NE((*table)[left].priority.value(), (*table)[right].priority.value());
    }
  }
}

TEST(RmPriorityTest, ABandTooSmallIsRefused) {
  const std::array<TaskSpec, 3> specs{task(1, 1'000), task(2, 2'000), task(3, 3'000)};
  const core::expected<std::vector<PriorityAssignment>> table =
      assign_rate_monotonic_priorities(specs, core::Priority{50}, core::Priority{51});
  ASSERT_FALSE(table.has_value());
  EXPECT_EQ(table.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TEST(RmPriorityTest, AnInvertedBandIsRefused) {
  const std::array<TaskSpec, 1> specs{task(1, 1'000)};
  const core::expected<std::vector<PriorityAssignment>> table =
      assign_rate_monotonic_priorities(specs, core::Priority{60}, core::Priority{50});
  ASSERT_FALSE(table.has_value());
  EXPECT_EQ(table.error(), core::ErrorCode::kConfigValueOutOfRange);
}

} // namespace
} // namespace volt::sched
