#pragma once

#include "volt/core/duration.hpp"
#include "volt/sched/task_spec.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace volt::sched {

/// One execution window in a time-triggered table.
///
/// Offsets are relative to the start of the hyperperiod, never to a wall
/// clock: the same table then runs on every node once each agrees on an
/// origin, which is what SPEC 9.2 means by a distributed hyperperiod.
struct ScheduleSlot {
  TaskId task{0};
  /// Which execution lane runs it. A lane is one dispatcher, pinned to one
  /// core in deployment, so two slots on the same lane are strictly ordered.
  std::uint32_t lane = 0;
  /// Start of the window, from the hyperperiod origin.
  core::Duration offset;
  /// Length of the window. The job owns the lane for exactly this long.
  core::Duration budget;

  [[nodiscard]] core::Duration end() const noexcept {
    return core::Duration::from_ns(offset.ns() + budget.ns());
  }
};

/// A complete time-triggered schedule: what runs, where, and when, for one
/// hyperperiod, repeated forever.
struct ScheduleTable {
  core::Duration hyperperiod;
  std::uint32_t lane_count = 0;
  /// Sorted by offset, then lane. The order is part of the table so that two
  /// tools reading the same file walk it identically.
  std::vector<ScheduleSlot> slots;
};

/// A task as the generator sees it: a period and the CPU it needs, nothing
/// about priorities, because a time-triggered table has none.
struct ScheduledTask {
  TaskId id{0};
  std::string name;
  core::Duration period;
  /// Window length to reserve, the declared budget of SPEC 9.1.
  core::Duration wcet;
};

/// `after` may not start until `before` has finished and `min_gap` has
/// passed.
///
/// The gap is what SPEC 38 asks for: a consumer activated a little after its
/// producer finished reads a fresh result instead of racing it. The rule is
/// cyclic, because the table repeats: the last instance of `before` in one
/// hyperperiod also precedes the first instance of `after` in the next.
struct Precedence {
  TaskId before{0};
  TaskId after{0};
  core::Duration min_gap;
};

/// Two tasks that may never run at the same time, on any lane.
///
/// Lane exclusion is free - one dispatcher runs one job at a time - so this
/// exists for what lanes cannot express: a device, a bus or a lock that two
/// tasks on different cores would otherwise touch together.
struct Exclusion {
  TaskId first{0};
  TaskId second{0};
};

/// Everything the generator has to satisfy.
struct ScheduleConstraints {
  /// Parallel dispatchers available, one per reserved core.
  std::uint32_t lane_count = 1;
  std::vector<ScheduledTask> tasks;
  std::vector<Precedence> precedences;
  std::vector<Exclusion> exclusions;
};

} // namespace volt::sched
