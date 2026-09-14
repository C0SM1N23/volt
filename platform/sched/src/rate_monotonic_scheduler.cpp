#include "volt/sched/rate_monotonic_scheduler.hpp"

#include <algorithm>
#include <utility>

namespace volt::sched {

core::expected<void> RateMonotonicScheduler::add(const TaskSpec &spec, JobFunction job) {
  const core::expected<void> valid = validate_for_rate_monotonic(spec);
  if (!valid.has_value()) {
    return valid;
  }
  return runner_.add(spec, std::move(job));
}

core::expected<void> RateMonotonicScheduler::start() {
  if (runner_.started()) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  const std::vector<TaskSpec> specs = runner_.specs();
  core::expected<std::vector<PriorityAssignment>> assigned =
      assign_rate_monotonic_priorities(specs, config_.priority_floor, config_.priority_ceiling);
  if (!assigned.has_value()) {
    return std::unexpected{assigned.error()};
  }
  priorities_ = std::move(*assigned);

  const auto configure = [this](const TaskSpec &spec) {
    const auto assignment = std::ranges::find_if(
        priorities_, [&spec](const PriorityAssignment &entry) { return entry.task == spec.id; });
    return pal::ThreadConfig{.name = spec.name,
                             .policy = pal::SchedulingPolicy::kFifo,
                             .priority = assignment->priority,
                             .cpu_mask = spec.affinity,
                             .stack_bytes = 0};
  };
  // Nothing for a fixed-priority thread to ask for beyond what it was
  // created with; the deadline class is where that hook earns its keep.
  return runner_.start(configure, nullptr);
}

} // namespace volt::sched
