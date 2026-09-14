// The independent checker, exercised the way the Perfetto validator is: the
// table goes out as a file and comes back judged by a script that shares no
// code with the generator. A validator nobody has seen reject anything is
// not evidence, so every rule is broken on purpose here and the script has
// to notice each time.

#include "volt/sched/schedule_generator.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>

namespace volt::sched {
namespace {

constexpr TaskId kBrake{1};
constexpr TaskId kFusion{2};
constexpr TaskId kDynamics{3};

[[nodiscard]] ScheduledTask scheduled(TaskId identifier, std::string name, std::int64_t period_us,
                                      std::int64_t wcet_us) {
  return ScheduledTask{.id = identifier,
                       .name = std::move(name),
                       .period = core::Duration::from_us(period_us),
                       .wcet = core::Duration::from_us(wcet_us)};
}

[[nodiscard]] ScheduleConstraints constraints() {
  return ScheduleConstraints{.lane_count = 2,
                             .tasks = {scheduled(kBrake, "brake-control", 1'000, 400),
                                       scheduled(kFusion, "sensor-fusion", 5'000, 610),
                                       scheduled(kDynamics, "vehicle-dynamics", 10'000, 1'720)},
                             .precedences = {Precedence{.before = kFusion,
                                                        .after = kBrake,
                                                        .min_gap = core::Duration::from_us(200)}},
                             .exclusions = {}};
}

void write_constraints(const std::string &path) {
  std::ofstream out{path};
  out << "lane_count: 2\ntasks:\n";
  for (const ScheduledTask &task : constraints().tasks) {
    out << std::format("  - id: {}\n    name: {}\n    period_us: {}\n    wcet_us: {}\n",
                       task.id.value(), task.name, task.period.ns() / 1'000,
                       task.wcet.ns() / 1'000);
  }
  out << "precedences:\n  - before: 2\n    after: 1\n    min_gap_us: 200\n";
}

void write_table(const std::string &path, const ScheduleTable &table) {
  std::ofstream out{path};
  out << std::format("hyperperiod_us: {}\nlane_count: {}\nslots:\n", table.hyperperiod.ns() / 1'000,
                     table.lane_count);
  for (const ScheduleSlot &slot : table.slots) {
    out << std::format("  - task: {}\n    lane: {}\n    offset_us: {}\n    budget_us: {}\n",
                       slot.task.value(), slot.lane, slot.offset.ns() / 1'000,
                       slot.budget.ns() / 1'000);
  }
}

/// Runs the script and returns its exit status, or nothing when there is no
/// interpreter to run it with.
[[nodiscard]] std::optional<std::int32_t> run_validator(pal::IPlatform &platform,
                                                        const std::string &constraints_path,
                                                        const std::string &table_path) {
  const std::array<std::string_view, 3> arguments{VOLT_SCHEDULE_VALIDATOR, constraints_path,
                                                  table_path};
  core::expected<std::unique_ptr<pal::IProcess>> validator = platform.spawn_process(
      pal::ProcessConfig{.executable = "/usr/bin/python3", .arguments = arguments});
  if (!validator.has_value()) {
    return std::nullopt;
  }
  const core::expected<pal::ProcessExit> exit = (*validator)->wait();
  if (!exit.has_value() || exit->reason != pal::ExitReason::kReturned) {
    return std::nullopt;
  }
  return exit->code;
}

class ScheduleValidatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    const core::expected<ScheduleTable> generated = generate_schedule(constraints());
    ASSERT_TRUE(generated.has_value());
    table_ = *generated;
    constraints_path_ = std::string{VOLT_SCHEDULE_SCRATCH} + "-constraints.yaml";
    table_path_ = std::string{VOLT_SCHEDULE_SCRATCH} + "-table.yaml";
    write_constraints(constraints_path_);
  }

  /// Writes the table with `break_it` applied and returns the verdict.
  [[nodiscard]] std::optional<std::int32_t>
  verdict_after(const std::function<void(ScheduleTable &)> &break_it) {
    ScheduleTable corrupted = table_;
    break_it(corrupted);
    write_table(table_path_, corrupted);
    return run_validator(platform_, constraints_path_, table_path_);
  }

  pal::posix::PosixPlatform platform_;
  ScheduleTable table_;
  std::string constraints_path_;
  std::string table_path_;
};

TEST_F(ScheduleValidatorTest, AcceptsWhatTheGeneratorProduced) {
  const std::optional<std::int32_t> verdict = verdict_after([](ScheduleTable &) {});
  if (!verdict.has_value()) {
    GTEST_SKIP() << "no python3 to run the independent validator with";
  }
  EXPECT_EQ(*verdict, 0) << "the independent validator rejected a table the generator vouched for";
}

TEST_F(ScheduleValidatorTest, RejectsEveryKindOfBrokenTable) {
  // One corruption per rule. Each is a table somebody could plausibly write
  // by hand or produce with a subtly wrong solver.
  const std::array<std::pair<std::string_view, std::function<void(ScheduleTable &)>>, 7> breakages{
      {{"a hyperperiod that is not the least common multiple",
        [](ScheduleTable &table) { table.hyperperiod = core::Duration::from_us(15'000); }},
       {"a slot reserving more than the task declared",
        [](ScheduleTable &table) { table.slots[0].budget = core::Duration::from_us(900); }},
       {"two jobs on one lane at once",
        [](ScheduleTable &table) {
          table.slots[1].lane = table.slots[0].lane;
          table.slots[1].offset = table.slots[0].offset;
        }},
       {"an instance pushed past the period it belongs to",
        [](ScheduleTable &table) { table.slots.back().offset = core::Duration::from_us(19'900); }},
       {"an activation that simply never happens",
        [](ScheduleTable &table) { table.slots.pop_back(); }},
       {"a lane the table does not have", [](ScheduleTable &table) { table.slots[0].lane = 9; }},
       {"a consumer activated inside its producer's gap", [](ScheduleTable &table) {
          // Put the producer 100 us before a brake activation, inside the
          // 200 us the rule reserves.
          for (ScheduleSlot &slot : table.slots) {
            if (slot.task == kFusion) {
              slot.offset = core::Duration::from_us(290);
              break;
            }
          }
        }}}};

  for (const auto &[description, break_it] : breakages) {
    const std::optional<std::int32_t> verdict = verdict_after(break_it);
    if (!verdict.has_value()) {
      GTEST_SKIP() << "no python3 to run the independent validator with";
    }
    EXPECT_EQ(*verdict, 1) << "the validator accepted " << description;
  }
}

} // namespace
} // namespace volt::sched
