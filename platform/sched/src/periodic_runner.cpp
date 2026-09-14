#include "volt/sched/detail/periodic_runner.hpp"

#include "volt/trace/tracer.hpp"

#include <algorithm>
#include <utility>

namespace volt::sched::detail {
namespace {

/// The whole life of one task thread: ask for whatever only this thread can
/// ask for, register with the tracer, fix the origin, then one step per
/// activation until the stop flag or the disarm.
void run_task(TaskLoop &loop, const std::atomic<bool> &running, std::string_view name) {
  // The ring exists before the first activation so no trace point inside a
  // cycle ever allocates (SPEC 8.4).
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

} // namespace

PeriodicRunner::~PeriodicRunner() { stop(); }

core::expected<void> PeriodicRunner::add(const TaskSpec &spec, JobFunction job) {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
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

core::expected<std::unique_ptr<pal::IThread>>
PeriodicRunner::launch(Slot &slot, const pal::ThreadConfig &wanted, const ThreadPreparer &prepare) {
  TaskLoop *const loop = slot.loop.get();
  const std::atomic<bool> *const running = &running_;
  std::atomic<bool> *const degraded = &realtime_degraded_;
  const TaskSpec *const spec = &slot.spec;

  auto body = [loop, running, degraded, spec, prepare] {
    // Release so a reader of realtime_degraded() sees the flag together with
    // everything this thread did before giving up on the guarantee.
    const bool granted = !prepare || prepare(*spec).has_value();
    degraded->store(degraded->load(std::memory_order_relaxed) || !granted,
                    std::memory_order_release);
    run_task(*loop, *running, spec->name);
  };

  core::expected<std::unique_ptr<pal::IThread>> thread = platform_->create_thread(wanted, body);
  if (thread.has_value()) {
    return thread;
  }
  if (thread.error() != core::ErrorCode::kResourceUnavailable || require_realtime_) {
    return std::unexpected{thread.error()};
  }
  // Unprivileged development machines refuse the real-time policies. The
  // cadence and the instrumentation still mean something under the default
  // one; the degradation is recorded, never silent (SPEC 25 wants both
  // worlds measured).
  realtime_degraded_.store(true, std::memory_order_release);
  const pal::ThreadConfig best_effort{.name = slot.spec.name,
                                      .policy = pal::SchedulingPolicy::kOther,
                                      .priority = core::Priority{0},
                                      .cpu_mask = slot.spec.affinity,
                                      .stack_bytes = 0};
  return platform_->create_thread(best_effort, std::move(body));
}

core::expected<void> PeriodicRunner::start(const ThreadConfigurator &configure,
                                           const ThreadPreparer &prepare) {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  started_ = true;

  for (Slot &slot : slots_) {
    core::expected<std::unique_ptr<pal::ITimer>> timer = platform_->create_timer();
    if (!timer.has_value()) {
      stop();
      return std::unexpected{timer.error()};
    }
    slot.timer = std::move(*timer);
    slot.loop =
        std::make_unique<TaskLoop>(platform_->clock(), *slot.timer, slot.spec, std::move(slot.job),
                                   *slot.monitor, overrun_ ? &overrun_ : nullptr);
  }

  // The flag rises before any thread exists, so no task thread can check it
  // before it is true; release pairs with the acquire in each body.
  running_.store(true, std::memory_order_release);
  for (Slot &slot : slots_) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        launch(slot, configure(slot.spec), prepare);
    if (!thread.has_value()) {
      stop();
      return std::unexpected{thread.error()};
    }
    slot.thread = std::move(*thread);
  }
  return {};
}

void PeriodicRunner::stop() {
  // Release pairs with the bodies' acquire: whatever the caller wrote before
  // stop() is visible to a body that observes the flag down.
  running_.store(false, std::memory_order_release);
  for (Slot &slot : slots_) {
    if (slot.timer != nullptr) {
      // Disarming wakes the wait with an error, which is the stop path. A
      // timer that cannot even be disarmed has nothing useful to tell a
      // shutdown that must proceed regardless.
      [[maybe_unused]] const core::expected<void> disarmed = slot.timer->disarm();
    }
  }
  for (Slot &slot : slots_) {
    if (slot.thread != nullptr && slot.thread->joinable()) {
      [[maybe_unused]] const core::expected<void> joined = slot.thread->join();
    }
  }
}

core::expected<TaskStatsSnapshot> PeriodicRunner::stats(TaskId task) const {
  const auto slot =
      std::ranges::find_if(slots_, [task](const Slot &entry) { return entry.spec.id == task; });
  if (slot == slots_.end()) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  return slot->monitor->snapshot();
}

std::vector<TaskSpec> PeriodicRunner::specs() const {
  std::vector<TaskSpec> out;
  out.reserve(slots_.size());
  for (const Slot &slot : slots_) {
    out.push_back(slot.spec);
  }
  return out;
}

} // namespace volt::sched::detail
