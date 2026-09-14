#include "volt/sched/edf_scheduler.hpp"

#include <utility>

namespace volt::sched {

core::expected<void> EdfScheduler::add(const TaskSpec &spec, JobFunction job) {
  const core::expected<void> valid = validate_for_edf(spec);
  if (!valid.has_value()) {
    return valid;
  }
  return runner_.add(spec, std::move(job));
}

core::expected<void> EdfScheduler::start() {
  // The thread is created under the default policy and asks for its
  // reservation once it is running: SCHED_DEADLINE is set on a live thread,
  // and the kernel's admission test belongs to the thread that has to live
  // with the answer.
  const auto configure = [](const TaskSpec &spec) {
    return pal::ThreadConfig{.name = spec.name,
                             .policy = pal::SchedulingPolicy::kOther,
                             .priority = core::Priority{0},
                             // A deadline thread may not change its affinity
                             // afterwards, so the spec's mask is left out
                             // here and applied through cpusets instead.
                             .cpu_mask = 0,
                             .stack_bytes = 0};
  };
  const auto prepare = [this](const TaskSpec &spec) {
    return platform_->set_current_thread_deadline(pal::DeadlineParameters{
        .runtime = spec.wcet_budget, .deadline = spec.deadline, .period = spec.period});
  };
  return runner_.start(configure, prepare);
}

} // namespace volt::sched
