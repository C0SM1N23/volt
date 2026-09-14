#pragma once

#include "volt/sched/schedule.hpp"

#include <cstdint>
#include <string_view>

namespace volt::sched {

/// What is wrong with a table, if anything.
///
/// One value per rule, because "invalid" is useless to whoever has to fix
/// the table: the point of a checker is to name the broken rule.
enum class ScheduleDefect : std::uint8_t {
  kNone,
  /// The hyperperiod is not the least common multiple of the periods.
  kWrongHyperperiod,
  /// A slot names a task the constraints do not contain.
  kUnknownTask,
  /// A task does not appear once per period inside the hyperperiod.
  kWrongInstanceCount,
  /// An instance does not fit inside the period it belongs to.
  kOutsideReleaseWindow,
  /// A slot reserves something other than the task's declared budget.
  kWrongBudget,
  /// A lane runs two jobs at once.
  kLaneOverlap,
  /// A slot names a lane the table does not have.
  kLaneOutOfRange,
  /// Two tasks that may never overlap do.
  kExclusionViolated,
  /// A consumer starts before its producer finished, or too soon after.
  kPrecedenceViolated,
  /// Slots are not in the order the table promises.
  kUnsorted,
};

/// The verdict, with enough context to find the offending slot.
struct ScheduleVerdict {
  ScheduleDefect defect = ScheduleDefect::kNone;
  /// Slot the checker objected to, where one applies.
  std::size_t slot_index = 0;
  /// The other slot of the pair, for rules about two slots.
  std::size_t other_index = 0;

  [[nodiscard]] bool valid() const noexcept { return defect == ScheduleDefect::kNone; }
};

/// Returns the name of a defect, for a message a human reads.
[[nodiscard]] std::string_view to_string(ScheduleDefect defect) noexcept;

/// Checks a table against the constraints it claims to satisfy.
///
/// This is deliberately the slow, obvious implementation: every rule is
/// re-derived from the constraints and every pair of slots is compared, with
/// no index, no incremental state and nothing shared with the generator's
/// search. A verifier that reuses the solver's reasoning inherits the
/// solver's mistakes, which is the one thing it exists to prevent; an O(n^2)
/// pass over a table produced once, offline, costs nothing worth saving.
///
/// @thread any; reads both arguments and keeps no state
[[nodiscard]] ScheduleVerdict check_schedule(const ScheduleConstraints &constraints,
                                             const ScheduleTable &table);

/// Returns the hyperperiod a constraint set implies.
///
/// @errors kConfigValueOutOfRange for an empty task set or a non-positive
///         period, kInternalArithmeticOverflow when the multiple does not fit
[[nodiscard]] core::expected<core::Duration>
hyperperiod_of(const ScheduleConstraints &constraints) noexcept;

} // namespace volt::sched
