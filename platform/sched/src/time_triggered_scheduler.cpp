#include "volt/sched/time_triggered_scheduler.hpp"

#include "volt/trace/tracer.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace volt::sched {
namespace {

void trace_instant(trace::TraceEvent event, std::uint32_t argument) noexcept {
  if (trace::Tracer::instance().enabled()) {
    trace::emit(event, argument);
  }
}

} // namespace

TimeTriggeredScheduler::TimeTriggeredScheduler(pal::IPlatform &platform, ScheduleTable table,
                                               TimeTriggeredConfig config)
    : platform_{&platform}, table_{std::move(table)}, config_{config} {
  lanes_.resize(table_.lane_count);
  for (const ScheduleSlot &slot : table_.slots) {
    lanes_[slot.lane].slots.push_back(slot);
    bound_.try_emplace(slot.task.value());
  }
  // The table is sorted overall, so each lane's share is sorted too; saying
  // it here rather than assuming it keeps the dispatcher loop honest if the
  // table format ever changes.
  for (Lane &lane : lanes_) {
    std::ranges::sort(lane.slots, [](const ScheduleSlot &left, const ScheduleSlot &right) {
      return left.offset.ns() < right.offset.ns();
    });
  }
}

TimeTriggeredScheduler::~TimeTriggeredScheduler() { stop(); }

core::expected<void> TimeTriggeredScheduler::bind(TaskId task, JobFunction job) {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  const auto entry = bound_.find(task.value());
  if (entry == bound_.end()) {
    return std::unexpected{core::ErrorCode::kConfigInvalidValue};
  }
  if (entry->second.monitor != nullptr) {
    return std::unexpected{core::ErrorCode::kConfigDuplicateId};
  }
  entry->second.job = std::move(job);
  entry->second.monitor = std::make_unique<TaskMonitor>();
  return {};
}

bool TimeTriggeredScheduler::run_slot(const ScheduleSlot &slot, pal::ITimer &timer,
                                      std::int64_t target_ns) {
  pal::IClock &clock = platform_->clock();
  const std::int64_t before_ns = clock.monotonic().ns_since_epoch();
  if (target_ns > before_ns) {
    if (!timer.arm_once(core::Duration::from_ns(target_ns - before_ns)).has_value()) {
      return false;
    }
    if (!timer.wait().has_value()) {
      // A disarm on stop lands here; so does a timer that failed. Either
      // way this lane is done.
      return false;
    }
  }
  if (!running_.load(std::memory_order_acquire)) {
    return false;
  }

  Bound &bound = bound_[slot.task.value()];
  const std::int64_t woke_ns = clock.monotonic().ns_since_epoch();
  const std::int64_t late_ns = woke_ns - target_ns;
  bound.monitor->record_activation(late_ns > 0 ? static_cast<std::uint64_t>(late_ns) : 0, 0);
  trace_instant(trace::TraceEvent::kTaskActivate, slot.task.value());

  const std::int64_t cpu_before_ns = clock.thread_cpu().ns_since_epoch();
  {
    trace::TraceScope traced{trace::TraceEvent::kTaskStart, trace::TraceEvent::kTaskEnd,
                             slot.task.value()};
    bound.job();
  }
  const std::int64_t cpu_used_ns = clock.thread_cpu().ns_since_epoch() - cpu_before_ns;
  const std::int64_t response_ns = clock.monotonic().ns_since_epoch() - target_ns;

  // The slot is the whole contract: a job that runs past its window has both
  // missed its deadline and overrun its budget, because in this class they
  // are the same reservation.
  const bool missed = response_ns > slot.budget.ns();
  const bool overran = cpu_used_ns > slot.budget.ns();
  bound.monitor->record_completion(response_ns > 0 ? static_cast<std::uint64_t>(response_ns) : 0,
                                   cpu_used_ns > 0 ? static_cast<std::uint64_t>(cpu_used_ns) : 0,
                                   missed, overran);
  if (missed) {
    trace_instant(trace::TraceEvent::kDeadlineMiss, slot.task.value());
  }
  if (overran) {
    trace_instant(trace::TraceEvent::kBudgetOverrun, slot.task.value());
  }
  return true;
}

bool TimeTriggeredScheduler::run_cycle(Lane &lane, std::int64_t cycle_start_ns) {
  for (const ScheduleSlot &slot : lane.slots) {
    if (!run_slot(slot, *lane.timer, cycle_start_ns + slot.offset.ns())) {
      return false;
    }
  }
  return true;
}

void TimeTriggeredScheduler::run_lane(Lane &lane, std::string_view name) {
  static_cast<void>(trace::Tracer::prepare_current_thread(name));
  const std::int64_t hyperperiod_ns = table_.hyperperiod.ns();

  // Start at the first hyperperiod that has not gone by. Replaying instants
  // already past would run a burst of jobs back to back, which is the one
  // thing this class exists to avoid.
  const std::int64_t elapsed_ns = platform_->clock().monotonic().ns_since_epoch() - origin_ns_;
  std::int64_t cycle = elapsed_ns <= 0 ? 0 : (elapsed_ns + hyperperiod_ns - 1) / hyperperiod_ns;

  while (running_.load(std::memory_order_acquire)) {
    if (!run_cycle(lane, origin_ns_ + (cycle * hyperperiod_ns))) {
      return;
    }
    cycle += 1;
  }
}

core::expected<void> TimeTriggeredScheduler::start(core::Timestamp origin) {
  if (started_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  for (const auto &[task, bound] : bound_) {
    if (bound.monitor == nullptr) {
      return std::unexpected{core::ErrorCode::kConfigMissingField};
    }
  }
  started_ = true;
  origin_ns_ = origin.ns_since_epoch();

  for (Lane &lane : lanes_) {
    core::expected<std::unique_ptr<pal::ITimer>> timer = platform_->create_timer();
    if (!timer.has_value()) {
      stop();
      return std::unexpected{timer.error()};
    }
    lane.timer = std::move(*timer);
  }

  running_.store(true, std::memory_order_release);
  for (std::size_t index = 0; index < lanes_.size(); ++index) {
    const core::expected<void> launched = launch_lane(index);
    if (!launched.has_value()) {
      stop();
      return launched;
    }
  }
  return {};
}

core::expected<void> TimeTriggeredScheduler::launch_lane(std::size_t index) {
  const std::string name = std::format("volt-tt-{}", index);
  Lane &lane = lanes_[index];
  auto body = [this, &lane, name] { run_lane(lane, name); };
  // One lane is one core in deployment; the mask comes from the deployment
  // file, not from the table.
  const pal::ThreadConfig wanted{.name = name,
                                 .policy = pal::SchedulingPolicy::kFifo,
                                 .priority = config_.priority,
                                 .cpu_mask = 0,
                                 .stack_bytes = 0};
  core::expected<std::unique_ptr<pal::IThread>> thread = platform_->create_thread(wanted, body);
  if (!thread.has_value()) {
    if (thread.error() != core::ErrorCode::kResourceUnavailable || config_.require_realtime) {
      return std::unexpected{thread.error()};
    }
    // Same fallback the other two classes make, recorded the same way.
    realtime_degraded_.store(true, std::memory_order_release);
    const pal::ThreadConfig best_effort{.name = name,
                                        .policy = pal::SchedulingPolicy::kOther,
                                        .priority = core::Priority{0},
                                        .cpu_mask = 0,
                                        .stack_bytes = 0};
    thread = platform_->create_thread(best_effort, std::move(body));
  }
  if (!thread.has_value()) {
    return std::unexpected{thread.error()};
  }
  lane.thread = std::move(*thread);
  return {};
}

void TimeTriggeredScheduler::stop() {
  running_.store(false, std::memory_order_release);
  for (Lane &lane : lanes_) {
    if (lane.timer != nullptr) {
      [[maybe_unused]] const core::expected<void> disarmed = lane.timer->disarm();
    }
  }
  for (Lane &lane : lanes_) {
    if (lane.thread != nullptr && lane.thread->joinable()) {
      [[maybe_unused]] const core::expected<void> joined = lane.thread->join();
    }
  }
}

core::expected<TaskStatsSnapshot> TimeTriggeredScheduler::stats(TaskId task) const {
  const auto entry = bound_.find(task.value());
  if (entry == bound_.end() || entry->second.monitor == nullptr) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  return entry->second.monitor->snapshot();
}

} // namespace volt::sched
