#pragma once

#include "volt/core/error.hpp"
#include "volt/sched/schedule.hpp"

namespace volt::sched {

/// Steps the search may take before it gives up.
///
/// A bound rather than a timeout: an offline tool must produce the same
/// answer on every machine it runs on, and a wall-clock cutoff would make
/// the output depend on how busy the build server was.
inline constexpr std::uint64_t kScheduleSearchBound = 2'000'000;

/// Builds a time-triggered table that satisfies `constraints`.
///
/// Greedy with backtracking: instances are placed in order of how little
/// slack they have, each at the earliest instant that breaks no rule, and
/// the search reverses the last choice whenever it reaches a dead end. The
/// candidate instants are the interesting ones - a window opening, a slot
/// ending, a precedence gap expiring - rather than every point on a grid,
/// which is what keeps the search small.
///
/// That restriction makes the search incomplete: a schedule can exist that
/// this refuses to find. It cannot make it wrong. Every table is handed to
/// the independent checker before it is returned, so the only failure mode
/// left is an honest "no answer found".
///
/// @post   on success `check_schedule` accepts the result
/// @errors kConfigValueOutOfRange for constraints that describe no table at
///         all, kInternalArithmeticOverflow for periods whose multiple does
///         not fit, kResourceExhausted when the search bound runs out,
///         kInternalOutOfRange when the checker rejects what the search
///         produced, which is a defect in this file
[[nodiscard]] core::expected<ScheduleTable>
generate_schedule(const ScheduleConstraints &constraints);

} // namespace volt::sched
