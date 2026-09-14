#pragma once

#include "volt/core/error.hpp"
#include "volt/sched/task_spec.hpp"

#include <span>
#include <vector>

namespace volt::sched {

/// One task's place in the rate-monotonic order.
struct PriorityAssignment {
  TaskId task{0};
  core::Priority priority{0};
};

/// Assigns fixed priorities the rate-monotonic way: the shorter the period,
/// the higher the priority.
///
/// The mapping, spelled out:
///   - tasks are ordered by period ascending; ties fall back to deadline
///     ascending, then to task id, so the order is total and two builds of
///     the same configuration always produce the same table;
///   - the shortest period receives `ceiling`, each next task one less;
///   - every task gets a distinct priority, ties included. Two tasks sharing
///     a SCHED_FIFO priority would run in arrival order, which is exactly
///     the kind of scheduling decision this class exists to pin down.
///
/// The band matters: VOLT leaves the top of the range to interrupt threads
/// and the watchdog (SPEC 8.1), so the ceiling is a parameter, not 99.
///
/// @errors kConfigValueOutOfRange when the band cannot hold every task
[[nodiscard]] core::expected<std::vector<PriorityAssignment>>
assign_rate_monotonic_priorities(std::span<const TaskSpec> specs, core::Priority floor,
                                 core::Priority ceiling);

} // namespace volt::sched
