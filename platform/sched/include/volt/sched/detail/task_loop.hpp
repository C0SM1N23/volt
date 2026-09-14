#pragma once

#include "volt/core/error.hpp"
#include "volt/pal/clock.hpp"
#include "volt/pal/timer.hpp"
#include "volt/sched/task_monitor.hpp"
#include "volt/sched/task_spec.hpp"

#include <functional>
#include <utility>

namespace volt::sched {

/// Body of one periodic job.
using JobFunction = std::move_only_function<void()>;

/// Told when a job exceeds its declared budget, with the action the task's
/// policy demands. The deeper consequences - degradation, safe state - belong
/// to the layers above the scheduler; here the policy is decided, counted
/// and announced.
using OverrunHandler =
    std::function<void(TaskId, OverrunAction, core::Duration used, core::Duration budget)>;

namespace detail {

/// What one activation cycle did.
struct CycleOutcome {
  /// Timer expirations consumed: one job ran; anything above one is
  /// activations that passed while the previous job still executed.
  std::uint64_t expirations = 0;
};

/// The periodic engine of one task: precise activation, instrumentation and
/// overrun policy, with no thread of its own.
///
/// Separated from the thread on purpose. On the POSIX backend a thread wraps
/// `begin` + `step` in a loop; the simulation backend, whose cooperative
/// threads run only at join, drives `step` directly and still exercises every
/// line of the engine - which is how the 3.6-million-activation drift claim
/// of K2 gets verified in seconds instead of an hour.
///
/// Activation is drift-free by construction: the platform timer maintains
/// the cadence from one absolute origin (the POSIX backend arms a timerfd,
/// whose expirations the kernel schedules as origin + k*period), and the
/// theoretical activation instant is computed from that origin and the
/// running expiration count, never from "now". A relative sleep would add
/// its own lateness to every subsequent cycle; this cannot.
///
/// @thread one caller at a time
class TaskLoop final {
public:
  /// @pre every reference outlives this loop; `spec` already validated
  TaskLoop(pal::IClock &clock, pal::ITimer &timer, const TaskSpec &spec, JobFunction job,
           TaskMonitor &monitor, const OverrunHandler *overrun) noexcept
      : clock_{&clock}, timer_{&timer}, spec_{&spec}, job_{std::move(job)}, monitor_{&monitor},
        overrun_{overrun} {}

  /// Waits out the phase offset and starts the periodic cadence.
  ///
  /// @post the theoretical origin is fixed; activation k is due at
  ///       origin + (k+1) * period
  [[nodiscard]] core::expected<void> begin() noexcept;

  /// Runs one activation: wait, instrument, execute, judge.
  ///
  /// @errors whatever the timer wait reports; a disarmed timer is the stop
  ///         signal and arrives here as its error
  [[nodiscard]] core::expected<CycleOutcome> step() noexcept;

  /// Nanoseconds since epoch of the activation the next `step` waits for.
  [[nodiscard]] std::int64_t next_theoretical_ns() const noexcept {
    return origin_ns_ + ((static_cast<std::int64_t>(consumed_) + 1) * spec_->period.ns());
  }

private:
  /// Records wake-up lateness and skipped activations for one cycle.
  void note_activation(std::int64_t theoretical_ns, std::uint64_t skipped) noexcept;

  /// Applies deadline and budget judgement to one finished job.
  void judge_completion(std::int64_t theoretical_ns, std::int64_t cpu_used_ns,
                        std::int64_t done_ns) noexcept;

  pal::IClock *clock_;
  pal::ITimer *timer_;
  const TaskSpec *spec_;
  JobFunction job_;
  TaskMonitor *monitor_;
  const OverrunHandler *overrun_;
  std::int64_t origin_ns_ = 0;
  /// Expirations consumed so far; the theoretical timeline is a pure
  /// function of this count and the origin.
  std::uint64_t consumed_ = 0;
};

} // namespace detail
} // namespace volt::sched
