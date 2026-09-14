#pragma once

#include "volt/core/error.hpp"
#include "volt/pal/platform.hpp"
#include "volt/sched/detail/task_loop.hpp"
#include "volt/sched/rm_priority.hpp"
#include "volt/sched/task_monitor.hpp"
#include "volt/sched/task_spec.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace volt::sched {

/// How the scheduler places its threads in the real-time priority space.
/// Default band for the rate-monotonic tasks: the top of the SCHED_FIFO
/// range stays free for the watchdog and interrupt threads (SPEC 8.1),
/// which must outrank every task, and the bottom for housekeeping.
inline constexpr core::Priority kDefaultPriorityFloor{10};
inline constexpr core::Priority kDefaultPriorityCeiling{79};

struct SchedulerConfig {
  /// Lowest and highest SCHED_FIFO priority the rate-monotonic band may use.
  core::Priority priority_floor = kDefaultPriorityFloor;
  core::Priority priority_ceiling = kDefaultPriorityCeiling;
  /// When true, a platform that refuses the real-time policy fails start();
  /// when false the scheduler falls back to the default policy and says so
  /// through `realtime_degraded` - the timing still runs, the numbers are
  /// just measured under a scheduler that makes no promises.
  bool require_realtime = false;
};

/// The fixed-priority periodic class of SPEC 9.2: one thread per task,
/// priorities assigned rate-monotonically, activation by absolute-cadence
/// platform timers, instrumentation per SPEC 9.4.
///
/// @thread configure from one thread; start/stop from that thread; task
///         threads run between them
class RateMonotonicScheduler final {
public:
  /// @pre `platform` outlives this scheduler
  RateMonotonicScheduler(pal::IPlatform &platform, SchedulerConfig config) noexcept
      : platform_{&platform}, config_{config} {}

  // Task threads hold pointers into this object, so it stays put.
  RateMonotonicScheduler(const RateMonotonicScheduler &) = delete;
  RateMonotonicScheduler &operator=(const RateMonotonicScheduler &) = delete;
  RateMonotonicScheduler(RateMonotonicScheduler &&) = delete;
  RateMonotonicScheduler &operator=(RateMonotonicScheduler &&) = delete;

  /// Stops whatever still runs.
  ~RateMonotonicScheduler();

  /// Registers a task. Only before start().
  ///
  /// @errors kConfigInvalidValue / kConfigValueOutOfRange from validation,
  ///         kConfigDuplicateId for a reused id,
  ///         kResourceBusy once started
  [[nodiscard]] core::expected<void> add(const TaskSpec &spec, JobFunction job);

  /// Called on every budget overrun, from the overrunning task's thread.
  /// Set before start().
  void set_overrun_handler(OverrunHandler handler) { overrun_ = std::move(handler); }

  /// Assigns priorities and launches one thread per task. Once: the jobs
  /// move into their loops here, so a stopped scheduler is finished, not
  /// paused - restarting means rebuilding, which is what a supervisor does
  /// anyway (SPEC 8.1 restarts processes, not schedulers).
  ///
  /// @post   on success every task is armed and running
  /// @errors kConfigValueOutOfRange when the priority band is too small,
  ///         kResourceUnavailable when real-time policy is refused and the
  ///         configuration requires it, kResourceBusy on a second start,
  ///         plus whatever thread or timer creation reports
  [[nodiscard]] core::expected<void> start();

  /// Disarms every timer and joins every thread. Idempotent.
  void stop();

  /// Reports whether start() had to fall back from the real-time policy.
  [[nodiscard]] bool realtime_degraded() const noexcept { return realtime_degraded_; }

  /// A task's numbers so far.
  ///
  /// @errors kInternalOutOfRange for an unknown id
  [[nodiscard]] core::expected<TaskStatsSnapshot> stats(TaskId task) const;

  /// The priority table start() computed, for reports and tests.
  [[nodiscard]] std::span<const PriorityAssignment> priorities() const noexcept {
    return priorities_;
  }

private:
  struct Slot {
    TaskSpec spec;
    JobFunction job;
    std::unique_ptr<pal::ITimer> timer;
    std::unique_ptr<TaskMonitor> monitor;
    std::unique_ptr<detail::TaskLoop> loop;
    std::unique_ptr<pal::IThread> thread;
  };

  [[nodiscard]] core::expected<std::unique_ptr<pal::IThread>> launch(Slot &slot,
                                                                     core::Priority priority);

  pal::IPlatform *platform_;
  SchedulerConfig config_;
  OverrunHandler overrun_;
  std::vector<Slot> slots_;
  std::vector<PriorityAssignment> priorities_;
  std::atomic<bool> running_{false};
  bool started_ = false;
  bool realtime_degraded_ = false;
};

} // namespace volt::sched
