#pragma once

#include "volt/core/error.hpp"
#include "volt/core/timestamp.hpp"
#include "volt/pal/platform.hpp"
#include "volt/sched/detail/task_loop.hpp"
#include "volt/sched/schedule.hpp"
#include "volt/sched/task_monitor.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace volt::sched {

/// Where the lane dispatchers sit. A dispatcher only ever sleeps until its
/// next slot, so it belongs above the fixed-priority band: when the instant
/// the table names arrives, nothing else may be in the way. Still below the
/// watchdog and the interrupt threads of SPEC 8.1.
inline constexpr core::Priority kDefaultDispatcherPriority{85};

struct TimeTriggeredConfig {
  /// Priority the lane dispatchers run at.
  core::Priority priority = kDefaultDispatcherPriority;
  /// When true, a platform that refuses the real-time policy fails start().
  bool require_realtime = false;
};

/// The time-triggered class of SPEC 9.2: a table computed offline, executed
/// against a shared origin.
///
/// Nothing here decides anything at runtime. Each lane walks its own slots in
/// order, waits for the instant the table names, runs the job, and waits for
/// the next - so the execution of a node is a pure function of the table and
/// the origin. Give two nodes the same table and the same origin and their
/// hyperperiods line up, which is what makes end-to-end latency across a
/// cluster a property you can compute instead of measure (SPEC 38).
///
/// The origin is a parameter of `start` rather than something read from a
/// clock inside, so the gPTP time base of P42 replaces the local one without
/// this interface changing.
///
/// @thread bind from one thread; start/stop from that thread; lane threads
///         run between them
class TimeTriggeredScheduler final {
public:
  /// @pre `platform` outlives this scheduler; `table` has been accepted by
  ///      `check_schedule`
  TimeTriggeredScheduler(pal::IPlatform &platform, ScheduleTable table, TimeTriggeredConfig config);

  // Lane threads hold pointers into this object, so it stays put.
  TimeTriggeredScheduler(const TimeTriggeredScheduler &) = delete;
  TimeTriggeredScheduler &operator=(const TimeTriggeredScheduler &) = delete;
  TimeTriggeredScheduler(TimeTriggeredScheduler &&) = delete;
  TimeTriggeredScheduler &operator=(TimeTriggeredScheduler &&) = delete;

  ~TimeTriggeredScheduler();

  /// Attaches a body to every slot the table gives `task`.
  ///
  /// @errors kConfigInvalidValue when the table has no slot for that task,
  ///         kConfigDuplicateId when a body is already bound,
  ///         kResourceBusy once started
  [[nodiscard]] core::expected<void> bind(TaskId task, JobFunction job);

  /// Starts every lane against `origin`.
  ///
  /// A hyperperiod already past is skipped rather than replayed: the table
  /// says what happens at each instant, and instants that have gone by are
  /// not owed to anyone.
  ///
  /// @errors kConfigMissingField when a slot has no body bound,
  ///         kResourceBusy on a second start, plus whatever thread or timer
  ///         creation reports
  [[nodiscard]] core::expected<void> start(core::Timestamp origin);

  /// Disarms every lane and joins it. Idempotent.
  void stop();

  /// Reports whether start() had to fall back from the real-time policy.
  [[nodiscard]] bool realtime_degraded() const noexcept {
    return realtime_degraded_.load(std::memory_order_acquire);
  }

  /// @errors kInternalOutOfRange for a task the table does not contain
  [[nodiscard]] core::expected<TaskStatsSnapshot> stats(TaskId task) const;

  [[nodiscard]] const ScheduleTable &table() const noexcept { return table_; }

private:
  /// One dispatcher: the slots it owns, in table order, and the timer it
  /// sleeps on.
  struct Lane {
    std::vector<ScheduleSlot> slots;
    std::unique_ptr<pal::ITimer> timer;
    std::unique_ptr<pal::IThread> thread;
  };

  /// Everything one task needs, shared by all of its slots.
  struct Bound {
    JobFunction job;
    std::unique_ptr<TaskMonitor> monitor;
  };

  void run_lane(Lane &lane, std::string_view name);
  /// Runs one hyperperiod of a lane. Returns false when the lane must stop.
  [[nodiscard]] bool run_cycle(Lane &lane, std::int64_t cycle_start_ns);
  /// Creates and starts the dispatcher for one lane.
  [[nodiscard]] core::expected<void> launch_lane(std::size_t index);
  /// Runs one slot of one cycle. Returns false when the lane must stop.
  [[nodiscard]] bool run_slot(const ScheduleSlot &slot, pal::ITimer &timer, std::int64_t target_ns);

  pal::IPlatform *platform_;
  ScheduleTable table_;
  TimeTriggeredConfig config_;
  std::vector<Lane> lanes_;
  std::map<std::uint32_t, Bound> bound_;
  std::int64_t origin_ns_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<bool> realtime_degraded_{false};
  bool started_ = false;
};

} // namespace volt::sched
