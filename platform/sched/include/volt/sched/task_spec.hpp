#pragma once

#include "volt/core/duration.hpp"
#include "volt/core/error.hpp"
#include "volt/core/strong_id.hpp"
#include "volt/core/types.hpp"
#include "volt/pal/thread.hpp"

#include <cstdint>
#include <string>

namespace volt::sched {

namespace detail {
struct TaskIdTag;
}

/// Identifies a task across the scheduler, the trace and the reports.
using TaskId = core::StrongId<detail::TaskIdTag, std::uint32_t>;

/// How much a task's failure matters, which decides what an overrun may do
/// to it (SPEC 9.5).
enum class Criticality : std::uint8_t {
  kSafetyCritical,
  kHigh,
  kMedium,
  kLow,
  kBestEffort,
};

/// Which scheduling class runs the task (SPEC 9.2).
enum class SchedClass : std::uint8_t {
  kTimeTriggered,
  kRateMonotonic,
  kEdf,
  kSporadic,
};

/// What to do when a job exceeds its declared budget (SPEC 9.5).
enum class OverrunAction : std::uint8_t {
  /// Count it, raise the event, keep going.
  kLog,
  /// Abandon the job's remaining work. Never legal for safety-critical
  /// tasks: a brake command half-computed is worse than one delivered late.
  kKillJob,
  /// Complete, then ask the layer above to shed functionality.
  kDegrade,
  /// Escalate to the safe state immediately.
  kSafeState,
};

/// Everything the scheduler needs to know about one task, exactly the model
/// of SPEC 9.1.
struct TaskSpec {
  TaskId id{0};
  std::string name;
  /// Zero means sporadic - activated by events, not by this scheduler class.
  core::Duration period;
  /// May exceed the period (SPEC 9.1 permits it); zero is invalid.
  core::Duration deadline;
  /// Declared budget for admission and overrun detection, not measured WCET.
  core::Duration wcet_budget;
  /// Used by classes with externally fixed priorities; the rate-monotonic
  /// class assigns its own and ignores this field.
  core::Priority priority{0};
  Criticality criticality = Criticality::kBestEffort;
  pal::CpuMask affinity = 0;
  SchedClass klass = SchedClass::kRateMonotonic;
  /// First activation delay inside the hyperperiod, for time-triggered
  /// tables; the rate-monotonic class honours it as a phase shift.
  core::Duration offset;
  OverrunAction on_overrun = OverrunAction::kLog;
};

/// Rejects a spec the rate-monotonic class could not honour.
///
/// @errors kConfigValueOutOfRange for a non-positive period, deadline or
///         budget, a budget beyond the period, or a negative offset;
///         kConfigInvalidValue for a class this scheduler does not run, an
///         empty name, or a kill policy on a safety-critical task
[[nodiscard]] core::expected<void> validate_for_rate_monotonic(const TaskSpec &spec) noexcept;

} // namespace volt::sched
