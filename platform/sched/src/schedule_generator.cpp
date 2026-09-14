#include "volt/sched/schedule_generator.hpp"

#include "volt/sched/schedule_checker.hpp"

#include <algorithm>
#include <vector>

namespace volt::sched {
namespace {

/// One release of one task, with the interval its start may fall in.
struct Instance {
  std::size_t task_index = 0;
  std::int64_t window_start = 0;
  /// Latest start that still lets the job finish inside its own period.
  std::int64_t latest_start = 0;
};

/// Returns whether two half-open windows share an instant.
[[nodiscard]] bool overlaps(std::int64_t left_start, std::int64_t left_end,
                            std::int64_t right_start, std::int64_t right_end) noexcept {
  return left_start < right_end && right_start < left_end;
}

/// Returns whether `instant` falls in the cyclic window [from, from + length).
[[nodiscard]] bool inside_cyclic_window(std::int64_t instant, std::int64_t from,
                                        std::int64_t length, std::int64_t hyperperiod) noexcept {
  if (length <= 0) {
    return false;
  }
  if (length >= hyperperiod) {
    return true;
  }
  const std::int64_t shifted = ((instant - from) % hyperperiod + hyperperiod) % hyperperiod;
  return shifted < length;
}

/// The backtracking placement search.
class Search final {
public:
  Search(const ScheduleConstraints &constraints, std::int64_t hyperperiod) noexcept
      : constraints_{&constraints}, hyperperiod_{hyperperiod} {}

  /// Expands every task into its releases, tightest slack first.
  void expand();

  /// Places every instance, or reports why it could not.
  [[nodiscard]] core::expected<void> run() { return place(0); }

  [[nodiscard]] std::vector<ScheduleSlot> take() noexcept { return std::move(placed_); }

private:
  [[nodiscard]] core::expected<void> place(std::size_t index);
  [[nodiscard]] core::expected<void> place_at(std::size_t index, const ScheduledTask &task,
                                              std::int64_t start);
  [[nodiscard]] core::expected<void> attempt(std::size_t index, const ScheduleSlot &slot);
  [[nodiscard]] bool meets_placed_task(const ScheduleSlot &slot, TaskId partner) const noexcept;
  [[nodiscard]] bool pair_respects(const Precedence &rule, const ScheduleSlot &slot,
                                   const ScheduleSlot &other) const noexcept;
  void add_precedence_candidates(std::vector<std::int64_t> &starts, const ScheduledTask &task,
                                 const ScheduleSlot &other) const;
  [[nodiscard]] bool lane_is_free(const ScheduleSlot &slot) const noexcept;
  [[nodiscard]] bool exclusions_hold(const ScheduleSlot &slot) const noexcept;
  [[nodiscard]] bool precedences_hold(const ScheduleSlot &slot) const noexcept;
  [[nodiscard]] std::vector<std::int64_t> candidates(const Instance &instance) const;

  [[nodiscard]] bool fits(const ScheduleSlot &slot) const noexcept {
    return lane_is_free(slot) && exclusions_hold(slot) && precedences_hold(slot);
  }

  const ScheduleConstraints *constraints_;
  std::int64_t hyperperiod_;
  std::vector<Instance> instances_;
  std::vector<ScheduleSlot> placed_;
  std::uint64_t steps_ = 0;
};

void Search::expand() {
  for (std::size_t index = 0; index < constraints_->tasks.size(); ++index) {
    const ScheduledTask &task = constraints_->tasks[index];
    for (std::int64_t release = 0; release < hyperperiod_ / task.period.ns(); ++release) {
      const std::int64_t start = release * task.period.ns();
      instances_.push_back(Instance{.task_index = index,
                                    .window_start = start,
                                    .latest_start = start + task.period.ns() - task.wcet.ns()});
    }
  }
  // Least slack first: an instance with nowhere to move is placed while the
  // table is still empty enough to take it. Ties resolve on the window and
  // then the task id, so the same constraints always give the same table.
  std::ranges::sort(instances_, [this](const Instance &left, const Instance &right) {
    const std::int64_t left_slack = left.latest_start - left.window_start;
    const std::int64_t right_slack = right.latest_start - right.window_start;
    if (left_slack != right_slack) {
      return left_slack < right_slack;
    }
    if (left.window_start != right.window_start) {
      return left.window_start < right.window_start;
    }
    return constraints_->tasks[left.task_index].id.value() <
           constraints_->tasks[right.task_index].id.value();
  });
}

bool Search::lane_is_free(const ScheduleSlot &slot) const noexcept {
  return std::ranges::none_of(placed_, [&slot](const ScheduleSlot &other) {
    return other.lane == slot.lane &&
           overlaps(slot.offset.ns(), slot.end().ns(), other.offset.ns(), other.end().ns());
  });
}

bool Search::meets_placed_task(const ScheduleSlot &slot, TaskId partner) const noexcept {
  return std::ranges::any_of(placed_, [&slot, partner](const ScheduleSlot &other) {
    return other.task == partner &&
           overlaps(slot.offset.ns(), slot.end().ns(), other.offset.ns(), other.end().ns());
  });
}

bool Search::exclusions_hold(const ScheduleSlot &slot) const noexcept {
  return std::ranges::none_of(constraints_->exclusions, [this, &slot](const Exclusion &exclusion) {
    const bool involved = exclusion.first == slot.task || exclusion.second == slot.task;
    const TaskId partner = exclusion.first == slot.task ? exclusion.second : exclusion.first;
    return involved && meets_placed_task(slot, partner);
  });
}

bool Search::pair_respects(const Precedence &rule, const ScheduleSlot &slot,
                           const ScheduleSlot &other) const noexcept {
  // The new slot may be either end of the rule, and a pair is only ever
  // judged once both of its slots exist, so checking both directions here is
  // what makes the finished table satisfy every pair.
  const bool consumes = slot.task == rule.after && other.task == rule.before;
  const bool too_soon =
      consumes && inside_cyclic_window(slot.offset.ns(), other.offset.ns(),
                                       other.budget.ns() + rule.min_gap.ns(), hyperperiod_);
  const bool produces = slot.task == rule.before && other.task == rule.after;
  const bool shadows =
      produces && inside_cyclic_window(other.offset.ns(), slot.offset.ns(),
                                       slot.budget.ns() + rule.min_gap.ns(), hyperperiod_);
  return !too_soon && !shadows;
}

bool Search::precedences_hold(const ScheduleSlot &slot) const noexcept {
  for (const Precedence &rule : constraints_->precedences) {
    const bool holds =
        std::ranges::all_of(placed_, [this, &rule, &slot](const ScheduleSlot &other) {
          return pair_respects(rule, slot, other);
        });
    if (!holds) {
      return false;
    }
  }
  return true;
}

void Search::add_precedence_candidates(std::vector<std::int64_t> &starts, const ScheduledTask &task,
                                       const ScheduleSlot &other) const {
  for (const Precedence &rule : constraints_->precedences) {
    if (rule.before == other.task && rule.after == task.id) {
      // As a consumer: the first instant the producer's gap has expired.
      starts.push_back(other.end().ns() + rule.min_gap.ns());
    }
    if (rule.before == task.id && rule.after == other.task) {
      // As a producer: the latest start whose blackout window still ends
      // before this consumer begins. Without it a producer can only be tried
      // where something else happens to end, and a table that does exist
      // goes unfound.
      starts.push_back(other.offset.ns() - task.wcet.ns() - rule.min_gap.ns());
    }
  }
}

std::vector<std::int64_t> Search::candidates(const Instance &instance) const {
  // The instants worth trying are the ones where the obstacles end: the
  // window opening, a placed slot finishing, a precedence gap expiring.
  // Anything between two of those is blocked by whatever blocks its
  // predecessor, so a finer grid would only cost time.
  const ScheduledTask &task = constraints_->tasks[instance.task_index];
  std::vector<std::int64_t> starts{instance.window_start};
  for (const ScheduleSlot &other : placed_) {
    starts.push_back(other.end().ns());
    add_precedence_candidates(starts, task, other);
  }
  std::ranges::sort(starts);
  const auto duplicates = std::ranges::unique(starts);
  starts.erase(duplicates.begin(), duplicates.end());
  std::erase_if(starts, [&instance](std::int64_t start) {
    return start < instance.window_start || start > instance.latest_start;
  });
  return starts;
}

core::expected<void> Search::attempt(std::size_t index, const ScheduleSlot &slot) {
  steps_ += 1;
  if (steps_ > kScheduleSearchBound) {
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }
  if (!fits(slot)) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  placed_.push_back(slot);
  const core::expected<void> rest = place(index + 1);
  if (rest.has_value()) {
    return rest;
  }
  placed_.pop_back();
  return std::unexpected{rest.error()};
}

core::expected<void> Search::place_at(std::size_t index, const ScheduledTask &task,
                                      std::int64_t start) {
  for (std::uint32_t lane = 0; lane < constraints_->lane_count; ++lane) {
    const core::expected<void> tried =
        attempt(index, ScheduleSlot{.task = task.id,
                                    .lane = lane,
                                    .offset = core::Duration::from_ns(start),
                                    .budget = task.wcet});
    // Exhausting the bound ends the whole search; a lane that simply did not
    // fit is the next lane's turn.
    if (tried.has_value() || tried.error() == core::ErrorCode::kResourceExhausted) {
      return tried;
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

core::expected<void> Search::place(std::size_t index) {
  if (index == instances_.size()) {
    return {};
  }
  const Instance &instance = instances_[index];
  const ScheduledTask &task = constraints_->tasks[instance.task_index];

  for (const std::int64_t start : candidates(instance)) {
    const core::expected<void> placed = place_at(index, task, start);
    if (placed.has_value() || placed.error() == core::ErrorCode::kResourceExhausted) {
      return placed;
    }
  }
  return std::unexpected{core::ErrorCode::kConfigCyclicDependency};
}

} // namespace

core::expected<ScheduleTable> generate_schedule(const ScheduleConstraints &constraints) {
  const core::expected<core::Duration> hyperperiod = hyperperiod_of(constraints);
  if (!hyperperiod.has_value()) {
    return std::unexpected{hyperperiod.error()};
  }
  if (constraints.lane_count == 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }

  Search search{constraints, hyperperiod->ns()};
  search.expand();
  const core::expected<void> placed = search.run();
  if (!placed.has_value()) {
    return std::unexpected{placed.error()};
  }

  ScheduleTable table{
      .hyperperiod = *hyperperiod, .lane_count = constraints.lane_count, .slots = search.take()};
  std::ranges::sort(table.slots, [](const ScheduleSlot &left, const ScheduleSlot &right) {
    return left.offset.ns() != right.offset.ns() ? left.offset.ns() < right.offset.ns()
                                                 : left.lane < right.lane;
  });

  // The search is not trusted to be right about its own output: a table only
  // leaves this function once the checker, which shares none of the
  // reasoning above, has agreed with it.
  const ScheduleVerdict verdict = check_schedule(constraints, table);
  if (!verdict.valid()) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  return table;
}

} // namespace volt::sched
