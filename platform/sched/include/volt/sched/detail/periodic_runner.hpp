#pragma once

#include "volt/core/error.hpp"
#include "volt/pal/platform.hpp"
#include "volt/sched/detail/task_loop.hpp"
#include "volt/sched/task_monitor.hpp"
#include "volt/sched/task_spec.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace volt::sched::detail {

/// Builds the thread configuration one task runs under.
using ThreadConfigurator = std::function<pal::ThreadConfig(const TaskSpec &)>;

/// Runs on the task's own thread before its first activation, for whatever
/// only that thread can ask for. A failure marks the runner degraded and the
/// task starts anyway: a reservation the kernel refuses is a weaker
/// guarantee, not a reason to leave the vehicle without the function.
using ThreadPreparer = std::function<core::expected<void>(const TaskSpec &)>;

/// One thread per task, each driven by a TaskLoop, with the instrumentation
/// of SPEC 9.4 and the shutdown path that disarms before it joins.
///
/// Everything the fixed-priority and the deadline classes share lives here;
/// the two differ only in the thread configuration they ask for and in what
/// the thread does to itself before it starts. Keeping that difference to
/// two callables is what stops the second class from being a copy of the
/// first.
///
/// @thread configure from one thread; start and stop from that same thread
class PeriodicRunner final {
public:
  /// @pre `platform` outlives this runner
  PeriodicRunner(pal::IPlatform &platform, bool require_realtime) noexcept
      : platform_{&platform}, require_realtime_{require_realtime} {}

  // Task threads hold pointers into this object, so it stays put.
  PeriodicRunner(const PeriodicRunner &) = delete;
  PeriodicRunner &operator=(const PeriodicRunner &) = delete;
  PeriodicRunner(PeriodicRunner &&) = delete;
  PeriodicRunner &operator=(PeriodicRunner &&) = delete;

  ~PeriodicRunner();

  /// Registers a task. Only before `start`.
  ///
  /// @errors kConfigDuplicateId for a reused id, kResourceBusy once started
  [[nodiscard]] core::expected<void> add(const TaskSpec &spec, JobFunction job);

  void set_overrun_handler(OverrunHandler handler) { overrun_ = std::move(handler); }

  /// Launches one thread per task. Once: the jobs move into their loops, so
  /// a stopped runner is finished rather than paused.
  ///
  /// @errors kResourceBusy on a second start, plus whatever thread or timer
  ///         creation reports
  [[nodiscard]] core::expected<void> start(const ThreadConfigurator &configure,
                                           const ThreadPreparer &prepare);

  /// Disarms every timer and joins every thread. Idempotent.
  void stop();

  [[nodiscard]] bool realtime_degraded() const noexcept {
    return realtime_degraded_.load(std::memory_order_acquire);
  }

  /// @errors kInternalOutOfRange for an unknown id
  [[nodiscard]] core::expected<TaskStatsSnapshot> stats(TaskId task) const;

  [[nodiscard]] std::vector<TaskSpec> specs() const;

  [[nodiscard]] bool started() const noexcept { return started_; }

private:
  struct Slot {
    TaskSpec spec;
    JobFunction job;
    std::unique_ptr<pal::ITimer> timer;
    std::unique_ptr<TaskMonitor> monitor;
    std::unique_ptr<TaskLoop> loop;
    std::unique_ptr<pal::IThread> thread;
  };

  [[nodiscard]] core::expected<std::unique_ptr<pal::IThread>>
  launch(Slot &slot, const pal::ThreadConfig &wanted, const ThreadPreparer &prepare);

  pal::IPlatform *platform_;
  bool require_realtime_;
  OverrunHandler overrun_;
  std::vector<Slot> slots_;
  std::atomic<bool> running_{false};
  std::atomic<bool> realtime_degraded_{false};
  bool started_ = false;
};

} // namespace volt::sched::detail
