#include "volt/sched/schedule_checker.hpp"

#include <algorithm>
#include <numeric>
#include <optional>
#include <vector>

namespace volt::sched {
namespace {

/// Returns whether the two half-open windows share any instant.
[[nodiscard]] bool overlaps(const ScheduleSlot &left, const ScheduleSlot &right) noexcept {
  return left.offset.ns() < right.end().ns() && right.offset.ns() < left.end().ns();
}

/// Returns whether `instant` falls in the cyclic window [from, from + length),
/// where the timeline wraps at `hyperperiod`.
///
/// Cyclic because the table repeats: the last producer instance of one
/// hyperperiod still precedes the first consumer instance of the next.
[[nodiscard]] bool inside_cyclic_window(std::int64_t instant, std::int64_t from,
                                        std::int64_t length, std::int64_t hyperperiod) noexcept {
  if (length <= 0 || hyperperiod <= 0) {
    return false;
  }
  if (length >= hyperperiod) {
    return true;
  }
  const std::int64_t shifted = ((instant - from) % hyperperiod + hyperperiod) % hyperperiod;
  return shifted < length;
}

/// Returns whether the two tasks may never run at the same time.
[[nodiscard]] bool excluded(const ScheduleConstraints &constraints, TaskId one,
                            TaskId other) noexcept {
  return std::ranges::any_of(constraints.exclusions, [one, other](const Exclusion &exclusion) {
    return (exclusion.first == one && exclusion.second == other) ||
           (exclusion.first == other && exclusion.second == one);
  });
}

[[nodiscard]] const ScheduledTask *find_task(const ScheduleConstraints &constraints,
                                             TaskId task) noexcept {
  const auto found = std::ranges::find_if(
      constraints.tasks, [task](const ScheduledTask &candidate) { return candidate.id == task; });
  return found == constraints.tasks.end() ? nullptr : &*found;
}

/// Checks the properties of one slot in isolation: known task, real lane,
/// declared budget, and an instance that fits its own period.
[[nodiscard]] ScheduleVerdict check_slot(const ScheduleConstraints &constraints,
                                         const ScheduleTable &table, std::size_t index,
                                         std::int64_t instance) noexcept {
  const ScheduleSlot &slot = table.slots[index];
  const ScheduledTask *const task = find_task(constraints, slot.task);
  if (task == nullptr) {
    return ScheduleVerdict{.defect = ScheduleDefect::kUnknownTask, .slot_index = index};
  }
  if (slot.lane >= table.lane_count) {
    return ScheduleVerdict{.defect = ScheduleDefect::kLaneOutOfRange, .slot_index = index};
  }
  if (slot.budget.ns() != task->wcet.ns()) {
    return ScheduleVerdict{.defect = ScheduleDefect::kWrongBudget, .slot_index = index};
  }
  const std::int64_t window_start = instance * task->period.ns();
  const std::int64_t window_end = window_start + task->period.ns();
  if (slot.offset.ns() < window_start || slot.end().ns() > window_end) {
    return ScheduleVerdict{.defect = ScheduleDefect::kOutsideReleaseWindow, .slot_index = index};
  }
  return ScheduleVerdict{};
}

/// Returns where `task` sits in the constraint list, or its size when absent.
[[nodiscard]] std::size_t position_of(const ScheduleConstraints &constraints,
                                      TaskId task) noexcept {
  std::size_t position = constraints.tasks.size();
  for (std::size_t candidate = 0; candidate < constraints.tasks.size(); ++candidate) {
    position = constraints.tasks[candidate].id == task ? candidate : position;
  }
  return position;
}

/// Checks every slot, counting instances per task as it goes.
[[nodiscard]] ScheduleVerdict count_and_check_slots(const ScheduleConstraints &constraints,
                                                    const ScheduleTable &table,
                                                    std::vector<std::int64_t> &seen) noexcept {
  for (std::size_t index = 0; index < table.slots.size(); ++index) {
    const std::size_t position = position_of(constraints, table.slots[index].task);
    if (position == constraints.tasks.size()) {
      return ScheduleVerdict{.defect = ScheduleDefect::kUnknownTask, .slot_index = index};
    }
    // Slots are sorted by offset, so the n-th slot of a task is its n-th
    // instance; the window check is what ties it to its own period.
    const ScheduleVerdict verdict = check_slot(constraints, table, index, seen[position]);
    if (!verdict.valid()) {
      return verdict;
    }
    seen[position] += 1;
  }
  return ScheduleVerdict{};
}

/// Checks that each task appears exactly once per period.
[[nodiscard]] ScheduleVerdict
check_instance_counts(const ScheduleConstraints &constraints, const ScheduleTable &table,
                      const std::vector<std::int64_t> &seen) noexcept {
  for (std::size_t position = 0; position < constraints.tasks.size(); ++position) {
    const ScheduledTask &task = constraints.tasks[position];
    const bool divides = task.period.ns() > 0 && table.hyperperiod.ns() % task.period.ns() == 0;
    if (!divides || seen[position] != table.hyperperiod.ns() / task.period.ns()) {
      return ScheduleVerdict{.defect = ScheduleDefect::kWrongInstanceCount};
    }
  }
  return ScheduleVerdict{};
}

/// Judges two slots that may or may not overlap.
[[nodiscard]] ScheduleVerdict check_pair(const ScheduleConstraints &constraints,
                                         const ScheduleTable &table, std::size_t left,
                                         std::size_t right) noexcept {
  const ScheduleSlot &one = table.slots[left];
  const ScheduleSlot &other = table.slots[right];
  if (!overlaps(one, other)) {
    return ScheduleVerdict{};
  }
  if (one.lane == other.lane) {
    return ScheduleVerdict{
        .defect = ScheduleDefect::kLaneOverlap, .slot_index = left, .other_index = right};
  }
  if (excluded(constraints, one.task, other.task)) {
    return ScheduleVerdict{
        .defect = ScheduleDefect::kExclusionViolated, .slot_index = left, .other_index = right};
  }
  return ScheduleVerdict{};
}

/// Judges one slot against every slot after it.
[[nodiscard]] ScheduleVerdict check_slot_against_later(const ScheduleConstraints &constraints,
                                                       const ScheduleTable &table,
                                                       std::size_t left) noexcept {
  for (std::size_t right = left + 1; right < table.slots.size(); ++right) {
    const ScheduleVerdict verdict = check_pair(constraints, table, left, right);
    if (!verdict.valid()) {
      return verdict;
    }
  }
  return ScheduleVerdict{};
}

/// Checks the rules that speak about two slots at once.
[[nodiscard]] ScheduleVerdict check_pairs(const ScheduleConstraints &constraints,
                                          const ScheduleTable &table) noexcept {
  for (std::size_t left = 0; left < table.slots.size(); ++left) {
    const ScheduleVerdict verdict = check_slot_against_later(constraints, table, left);
    if (!verdict.valid()) {
      return verdict;
    }
  }
  return ScheduleVerdict{};
}

/// Returns the first slot of `rule.after` that starts inside the blackout
/// window one producer instance casts, if any.
[[nodiscard]] std::optional<std::size_t>
consumer_inside_blackout(const ScheduleTable &table, const Precedence &rule,
                         const ScheduleSlot &producer) noexcept {
  const std::int64_t forbidden = producer.budget.ns() + rule.min_gap.ns();
  for (std::size_t consumer = 0; consumer < table.slots.size(); ++consumer) {
    const bool inside =
        table.slots[consumer].task == rule.after &&
        inside_cyclic_window(table.slots[consumer].offset.ns(), producer.offset.ns(), forbidden,
                             table.hyperperiod.ns());
    if (inside) {
      return consumer;
    }
  }
  return std::nullopt;
}

/// Checks one precedence rule against every producer instance.
[[nodiscard]] ScheduleVerdict check_precedence(const ScheduleTable &table,
                                               const Precedence &rule) noexcept {
  for (std::size_t producer = 0; producer < table.slots.size(); ++producer) {
    const std::optional<std::size_t> consumer =
        table.slots[producer].task != rule.before
            ? std::nullopt
            : consumer_inside_blackout(table, rule, table.slots[producer]);
    if (consumer.has_value()) {
      return ScheduleVerdict{.defect = ScheduleDefect::kPrecedenceViolated,
                             .slot_index = producer,
                             .other_index = *consumer};
    }
  }
  return ScheduleVerdict{};
}

/// Checks that no consumer starts inside a producer's window or its gap.
[[nodiscard]] ScheduleVerdict check_precedences(const ScheduleConstraints &constraints,
                                                const ScheduleTable &table) noexcept {
  for (const Precedence &rule : constraints.precedences) {
    const ScheduleVerdict verdict = check_precedence(table, rule);
    if (!verdict.valid()) {
      return verdict;
    }
  }
  return ScheduleVerdict{};
}

/// Returns whether the table is in the order it promises.
[[nodiscard]] bool sorted_by_offset_then_lane(const ScheduleTable &table) noexcept {
  return std::ranges::is_sorted(
      table.slots, [](const ScheduleSlot &left, const ScheduleSlot &right) {
        return left.offset.ns() != right.offset.ns() ? left.offset.ns() < right.offset.ns()
                                                     : left.lane < right.lane;
      });
}

} // namespace

std::string_view to_string(ScheduleDefect defect) noexcept {
  switch (defect) {
  case ScheduleDefect::kNone:
    return "none";
  case ScheduleDefect::kWrongHyperperiod:
    return "hyperperiod is not the least common multiple of the periods";
  case ScheduleDefect::kUnknownTask:
    return "slot names a task the constraints do not contain";
  case ScheduleDefect::kWrongInstanceCount:
    return "task does not appear once per period inside the hyperperiod";
  case ScheduleDefect::kOutsideReleaseWindow:
    return "instance does not fit inside the period it belongs to";
  case ScheduleDefect::kWrongBudget:
    return "slot reserves something other than the declared budget";
  case ScheduleDefect::kLaneOverlap:
    return "one lane runs two jobs at once";
  case ScheduleDefect::kLaneOutOfRange:
    return "slot names a lane the table does not have";
  case ScheduleDefect::kExclusionViolated:
    return "two tasks that may never overlap do";
  case ScheduleDefect::kPrecedenceViolated:
    return "consumer starts before its producer finished, or too soon after";
  case ScheduleDefect::kUnsorted:
    return "slots are not in the order the table promises";
  }
  return "unknown";
}

core::expected<core::Duration> hyperperiod_of(const ScheduleConstraints &constraints) noexcept {
  if (constraints.tasks.empty()) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  std::int64_t multiple = 1;
  for (const ScheduledTask &task : constraints.tasks) {
    if (task.period.ns() <= 0 || task.wcet.ns() <= 0 || task.wcet.ns() > task.period.ns()) {
      return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
    }
    const std::int64_t divisor = std::gcd(multiple, task.period.ns());
    std::int64_t product = 0;
    // Periods that are mutually prime multiply out fast; a table nobody
    // could store is a configuration mistake, not something to attempt.
    if (__builtin_mul_overflow(multiple / divisor, task.period.ns(), &product)) {
      return std::unexpected{core::ErrorCode::kInternalArithmeticOverflow};
    }
    multiple = product;
  }
  return core::Duration::from_ns(multiple);
}

ScheduleVerdict check_schedule(const ScheduleConstraints &constraints, const ScheduleTable &table) {
  const core::expected<core::Duration> expected_hyperperiod = hyperperiod_of(constraints);
  if (!expected_hyperperiod.has_value() || expected_hyperperiod->ns() != table.hyperperiod.ns() ||
      table.lane_count == 0) {
    return ScheduleVerdict{.defect = ScheduleDefect::kWrongHyperperiod};
  }
  if (!sorted_by_offset_then_lane(table)) {
    return ScheduleVerdict{.defect = ScheduleDefect::kUnsorted};
  }

  std::vector<std::int64_t> seen(constraints.tasks.size(), 0);
  const ScheduleVerdict slots = count_and_check_slots(constraints, table, seen);
  if (!slots.valid()) {
    return slots;
  }
  const ScheduleVerdict counts = check_instance_counts(constraints, table, seen);
  if (!counts.valid()) {
    return counts;
  }
  const ScheduleVerdict pairs = check_pairs(constraints, table);
  if (!pairs.valid()) {
    return pairs;
  }
  return check_precedences(constraints, table);
}

} // namespace volt::sched
