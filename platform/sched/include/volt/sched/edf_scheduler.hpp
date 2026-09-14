#pragma once

#include "volt/core/error.hpp"
#include "volt/pal/platform.hpp"
#include "volt/sched/detail/periodic_runner.hpp"
#include "volt/sched/task_monitor.hpp"
#include "volt/sched/task_spec.hpp"

#include <utility>

namespace volt::sched {

struct EdfConfig {
  /// When true, a kernel that refuses the reservation fails start(); when
  /// false the task runs under the default policy and says so through
  /// `realtime_degraded`.
  bool require_realtime = false;
};

/// The earliest-deadline-first class of SPEC 9.2, over the kernel's
/// SCHED_DEADLINE.
///
/// Each task asks the kernel for a reservation built from its own spec: the
/// declared budget becomes the runtime, the deadline and period come across
/// unchanged. From there the kernel, not VOLT, decides which runnable task
/// goes next, and it enforces the budget by throttling a thread that
/// overruns.
///
/// **What the kernel gives, and what it does not.** Points worth knowing
/// before choosing this class over the fixed-priority one:
///
/// - Setting the policy needs CAP_SYS_NICE. Without it every reservation is
///   refused, the tasks run under the default policy and `realtime_degraded`
///   reports it. The timing numbers are then measurements of SCHED_OTHER.
/// - The kernel runs an admission test against the bandwidth left on the
///   root domain (`/proc/sys/kernel/sched_rt_runtime_us`, 95% by default).
///   A task set that RTA proves schedulable can still be refused, because
///   that budget is shared with every other deadline task on the machine.
/// - A SCHED_DEADLINE thread may not change its CPU affinity, and it may not
///   fork. The affinity in a TaskSpec is therefore ignored by this class,
///   which is why deployment pins deadline tasks through cpusets instead.
/// - There is no priority inheritance. A deadline task blocking on a mutex
///   held by a lower-priority thread inherits nothing, so the SPEC 5.3 rule
///   against mutexes on the control path is not a style preference here.
/// - Throttling is silent from the thread's side: an overrunning job is
///   simply not scheduled until its next replenishment. VOLT's own budget
///   accounting still reports the overrun, which is what makes it visible.
///
/// Activation stays on the platform timer rather than on the kernel's
/// implicit periodic release, so all three classes are activated the same
/// way and the jitter comparison in docs/PERFORMANCE.md measures the
/// scheduling policy rather than three different clocks.
///
/// @thread configure from one thread; start/stop from that thread
class EdfScheduler final {
public:
  /// @pre `platform` outlives this scheduler
  EdfScheduler(pal::IPlatform &platform, EdfConfig config) noexcept
      : platform_{&platform}, runner_{platform, config.require_realtime} {}

  /// Registers a task. Only before start().
  ///
  /// @errors kConfigInvalidValue / kConfigValueOutOfRange from validation,
  ///         kConfigDuplicateId for a reused id, kResourceBusy once started
  [[nodiscard]] core::expected<void> add(const TaskSpec &spec, JobFunction job);

  void set_overrun_handler(OverrunHandler handler) {
    runner_.set_overrun_handler(std::move(handler));
  }

  /// Launches one thread per task, each asking the kernel for its own
  /// reservation before its first activation.
  ///
  /// @errors kResourceBusy on a second start, plus whatever thread or timer
  ///         creation reports
  [[nodiscard]] core::expected<void> start();

  void stop() { runner_.stop(); }

  /// Reports whether any task is running without the reservation it asked
  /// for, whether because the policy was refused or the bandwidth was gone.
  [[nodiscard]] bool realtime_degraded() const noexcept { return runner_.realtime_degraded(); }

  /// @errors kInternalOutOfRange for an unknown id
  [[nodiscard]] core::expected<TaskStatsSnapshot> stats(TaskId task) const {
    return runner_.stats(task);
  }

private:
  pal::IPlatform *platform_;
  detail::PeriodicRunner runner_;
};

} // namespace volt::sched
