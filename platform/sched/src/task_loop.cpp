#include "volt/sched/detail/task_loop.hpp"

#include "volt/trace/tracer.hpp"

namespace volt::sched::detail {
namespace {

/// One instant event, if tracing is on.
void trace_instant(trace::TraceEvent event, std::uint32_t argument) noexcept {
  if (trace::Tracer::instance().enabled()) {
    trace::emit(event, argument);
  }
}

} // namespace

core::expected<void> TaskLoop::begin() noexcept {
  if (spec_->offset.ns() > 0) {
    // The phase shift runs on the timer, not on a sleep: the loop's one
    // source of time remains the thing whose drift properties are known.
    const core::expected<void> armed = timer_->arm_once(spec_->offset);
    if (!armed.has_value()) {
      return armed;
    }
    const core::expected<std::uint64_t> waited = timer_->wait();
    if (!waited.has_value()) {
      return std::unexpected{waited.error()};
    }
  }
  origin_ns_ = clock_->monotonic().ns_since_epoch();
  consumed_ = 0;
  return timer_->arm_periodic(spec_->period);
}

void TaskLoop::judge_completion(std::int64_t theoretical_ns, std::int64_t cpu_used_ns,
                                std::int64_t done_ns) noexcept {
  const std::int64_t response_ns = done_ns - theoretical_ns;
  const bool missed = response_ns > spec_->deadline.ns();
  const bool overran = cpu_used_ns > spec_->wcet_budget.ns();
  monitor_->record_completion(response_ns > 0 ? static_cast<std::uint64_t>(response_ns) : 0,
                              cpu_used_ns > 0 ? static_cast<std::uint64_t>(cpu_used_ns) : 0, missed,
                              overran);
  if (missed) {
    trace_instant(trace::TraceEvent::kDeadlineMiss, spec_->id.value());
  }
  if (overran) {
    trace_instant(trace::TraceEvent::kBudgetOverrun, spec_->id.value());
    if (overrun_ != nullptr && *overrun_) {
      // The policy is decided by the spec, announced with the numbers; what
      // a degrade or safe-state decision does lives above the scheduler.
      (*overrun_)(spec_->id, spec_->on_overrun, core::Duration::from_ns(cpu_used_ns),
                  spec_->wcet_budget);
    }
  }
}

void TaskLoop::note_activation(std::int64_t theoretical_ns, std::uint64_t skipped) noexcept {
  const std::int64_t woke_ns = clock_->monotonic().ns_since_epoch();
  // The timer never fires early, so lateness is the whole story; a negative
  // difference would mean a broken platform clock and is clamped rather
  // than recorded as four exabytes of jitter.
  const std::int64_t late_ns = woke_ns - theoretical_ns;
  monitor_->record_activation(late_ns > 0 ? static_cast<std::uint64_t>(late_ns) : 0, skipped);
  if (skipped > 0) {
    monitor_->record_missed_activations(skipped);
    trace_instant(trace::TraceEvent::kDeadlineMiss, spec_->id.value());
  }
  trace_instant(trace::TraceEvent::kTaskActivate, spec_->id.value());
}

core::expected<CycleOutcome> TaskLoop::step() noexcept {
  const core::expected<std::uint64_t> expirations = timer_->wait();
  if (!expirations.has_value()) {
    // A disarmed timer lands here: it is the stop path, not a failure.
    return std::unexpected{expirations.error()};
  }

  // The kernel counts activations that fired while the previous job was
  // still running; each one is a job that never ran, and a job that never
  // ran certainly did not meet its deadline (SPEC 9.4 counts, never hides).
  const std::uint64_t skipped = *expirations - 1;
  consumed_ += *expirations;
  const std::int64_t theoretical_ns =
      origin_ns_ + (static_cast<std::int64_t>(consumed_) * spec_->period.ns());
  note_activation(theoretical_ns, skipped);

  const std::int64_t cpu_before_ns = clock_->thread_cpu().ns_since_epoch();
  {
    // Interval events pair around the job exactly as SPEC 8.4's timeline
    // expects; the scope closes even if the job returns early.
    trace::TraceScope traced{trace::TraceEvent::kTaskStart, trace::TraceEvent::kTaskEnd,
                             spec_->id.value()};
    job_();
  }
  const std::int64_t cpu_used_ns = clock_->thread_cpu().ns_since_epoch() - cpu_before_ns;
  const std::int64_t done_ns = clock_->monotonic().ns_since_epoch();
  const std::int64_t response_ns = done_ns - theoretical_ns;

  const bool missed = response_ns > spec_->deadline.ns();
  const bool overran = cpu_used_ns > spec_->wcet_budget.ns();
  monitor_->record_completion(response_ns > 0 ? static_cast<std::uint64_t>(response_ns) : 0,
                              cpu_used_ns > 0 ? static_cast<std::uint64_t>(cpu_used_ns) : 0, missed,
                              overran);
  if (missed) {
    trace_instant(trace::TraceEvent::kDeadlineMiss, spec_->id.value());
  }
  if (overran) {
    trace_instant(trace::TraceEvent::kBudgetOverrun, spec_->id.value());
    if (overrun_ != nullptr && *overrun_) {
      // The policy is decided by the spec, announced with the numbers; what
      // a degrade or safe-state decision does lives above the scheduler.
      (*overrun_)(spec_->id, spec_->on_overrun, core::Duration::from_ns(cpu_used_ns),
                  spec_->wcet_budget);
    }
  }
  return CycleOutcome{.expirations = *expirations};
}

} // namespace volt::sched::detail
