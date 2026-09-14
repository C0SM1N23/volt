#include "volt/sched/rate_monotonic_scheduler.hpp"

#include "volt/trace/tracer.hpp"

#include <algorithm>
#include <utility>

namespace volt::sched {

RateMonotonicScheduler::~RateMonotonicScheduler() { stop(); }

core::expected<void> RateMonotonicScheduler::add(const TaskSpec &spec, JobFunction job) {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  const core::expected<void> valid = validate_for_rate_monotonic(spec);
  if (!valid.has_value()) {
    return valid;
  }
  const bool duplicate =
      std::ranges::any_of(slots_, [&spec](const Slot &slot) { return slot.spec.id == spec.id; });
  if (duplicate) {
    return std::unexpected{core::ErrorCode::kConfigDuplicateId};
  }
  slots_.push_back(Slot{.spec = spec,
                        .job = std::move(job),
                        .timer = nullptr,
                        .monitor = std::make_unique<TaskMonitor>(),
                        .loop = nullptr,
                        .thread = nullptr});
  return {};
}

namespace {

/// The whole life of one task thread: register with the tracer, fix the
/// origin, then one step per activation until the stop flag or the disarm.
void run_task(detail::TaskLoop &loop, const std::atomic<bool> &running, std::string_view name) {
  // The ring exists before the first activation so no trace point inside
  // a cycle ever allocates (SPEC 8.4).
  static_cast<void>(trace::Tracer::prepare_current_thread(name));
  if (!loop.begin().has_value()) {
    return;
  }
  while (running.load(std::memory_order_acquire)) {
    // The disarm on stop surfaces as the wait's error and ends the loop,
    // between activations this thread costs nothing.
    if (!loop.step().has_value()) {
      return;
    }
  }
}

[[nodiscard]] pal::ThreadEntry task_body(detail::TaskLoop *loop, const std::atomic<bool> *running,
                                         std::string name) {
  return [loop, running, name = std::move(name)] { run_task(*loop, *running, name); };
}

} // namespace

core::expected<std::unique_ptr<pal::IThread>>
RateMonotonicScheduler::launch(Slot &slot, core::Priority priority) {
  const pal::ThreadConfig real_time{.name = slot.spec.name,
                                    .policy = pal::SchedulingPolicy::kFifo,
                                    .priority = priority,
                                    .cpu_mask = slot.spec.affinity,
                                    .stack_bytes = 0};
  core::expected<std::unique_ptr<pal::IThread>> thread =
      platform_->create_thread(real_time, task_body(slot.loop.get(), &running_, slot.spec.name));
  if (thread.has_value()) {
    return thread;
  }
  if (thread.error() != core::ErrorCode::kResourceUnavailable || config_.require_realtime) {
    return std::unexpected{thread.error()};
  }
  // Unprivileged development machines refuse SCHED_FIFO. The cadence and
  // the instrumentation still mean something under the default policy; the
  // degradation is recorded, never silent (SPEC 25 measures both worlds).
  realtime_degraded_ = true;
  const pal::ThreadConfig best_effort{.name = slot.spec.name,
                                      .policy = pal::SchedulingPolicy::kOther,
                                      .priority = core::Priority{0},
                                      .cpu_mask = slot.spec.affinity,
                                      .stack_bytes = 0};
  return platform_->create_thread(best_effort,
                                  task_body(slot.loop.get(), &running_, slot.spec.name));
}

core::expected<void> RateMonotonicScheduler::start() {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  started_ = true;
  std::vector<TaskSpec> specs;
  specs.reserve(slots_.size());
  for (const Slot &slot : slots_) {
    specs.push_back(slot.spec);
  }
  core::expected<std::vector<PriorityAssignment>> assigned =
      assign_rate_monotonic_priorities(specs, config_.priority_floor, config_.priority_ceiling);
  if (!assigned.has_value()) {
    return std::unexpected{assigned.error()};
  }
  priorities_ = std::move(*assigned);

  for (Slot &slot : slots_) {
    core::expected<std::unique_ptr<pal::ITimer>> timer = platform_->create_timer();
    if (!timer.has_value()) {
      stop();
      return std::unexpected{timer.error()};
    }
    slot.timer = std::move(*timer);
    slot.loop = std::make_unique<detail::TaskLoop>(platform_->clock(), *slot.timer, slot.spec,
                                                   std::move(slot.job), *slot.monitor,
                                                   overrun_ ? &overrun_ : nullptr);
  }

  // The flag rises before any thread exists, so no task thread can check it
  // before it is true; release pairs with the acquire in each body.
  running_.store(true, std::memory_order_release);
  for (Slot &slot : slots_) {
    const PriorityAssignment &assignment =
        *std::ranges::find_if(priorities_, [&slot](const PriorityAssignment &entry) {
          return entry.task == slot.spec.id;
        });
    core::expected<std::unique_ptr<pal::IThread>> thread = launch(slot, assignment.priority);
    if (!thread.has_value()) {
      stop();
      return std::unexpected{thread.error()};
    }
    slot.thread = std::move(*thread);
  }
  return {};
}

void RateMonotonicScheduler::stop() {
  // Release pairs with the bodies' acquire: whatever the caller wrote
  // before stop() is visible to a body that observes the flag down.
  running_.store(false, std::memory_order_release);
  for (Slot &slot : slots_) {
    if (slot.timer != nullptr) {
      // Disarming wakes the wait with an error, which is the stop path; a
      // timer that cannot even be disarmed has nothing useful to report to
      // a shutdown that must proceed regardless.
      [[maybe_unused]] const core::expected<void> disarmed = slot.timer->disarm();
    }
  }
  for (Slot &slot : slots_) {
    if (slot.thread != nullptr && slot.thread->joinable()) {
      [[maybe_unused]] const core::expected<void> joined = slot.thread->join();
    }
  }
}

core::expected<TaskStatsSnapshot> RateMonotonicScheduler::stats(TaskId task) const {
  const auto slot =
      std::ranges::find_if(slots_, [task](const Slot &entry) { return entry.spec.id == task; });
  if (slot == slots_.end()) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  return slot->monitor->snapshot();
}

} // namespace volt::sched
