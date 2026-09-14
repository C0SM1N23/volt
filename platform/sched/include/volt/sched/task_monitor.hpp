#pragma once

#include "volt/sched/hdr_histogram.hpp"

#include <atomic>
#include <cstdint>

namespace volt::sched {

/// The two quantiles every SPEC 9.4 report carries.
inline constexpr double kMedianQuantile = 0.50;
inline constexpr double kTailQuantile = 0.99;

/// A copy of one task's numbers at one instant, for reports.
struct TaskStatsSnapshot {
  std::uint64_t activations = 0;
  std::uint64_t deadline_misses = 0;
  std::uint64_t budget_overruns = 0;
  /// Activations the timer reported while the previous job still ran; each
  /// one is a job that never happened, counted, never hidden.
  std::uint64_t skipped_activations = 0;
  std::uint64_t jitter_p50_ns = 0;
  std::uint64_t jitter_p99_ns = 0;
  std::uint64_t jitter_max_ns = 0;
  std::uint64_t response_p50_ns = 0;
  std::uint64_t response_p99_ns = 0;
  std::uint64_t response_max_ns = 0;
  std::uint64_t execution_p50_ns = 0;
  std::uint64_t execution_p99_ns = 0;
  std::uint64_t execution_max_ns = 0;
};

/// The live instrumentation of one task: everything SPEC 9.4 collects, in
/// HDR histograms and counters the task thread updates wait-free.
///
/// @thread the task thread writes; any thread snapshots
class TaskMonitor final {
public:
  void record_activation(std::uint64_t jitter_ns, std::uint64_t skipped) noexcept {
    // Relaxed everywhere: monitoring counters order no data, and the task
    // thread is the only writer.
    activations_.fetch_add(1, std::memory_order_relaxed);
    skipped_.fetch_add(skipped, std::memory_order_relaxed);
    jitter_.record(jitter_ns);
  }

  void record_completion(std::uint64_t response_ns, std::uint64_t execution_ns,
                         bool missed_deadline, bool overran_budget) noexcept {
    response_.record(response_ns);
    execution_.record(execution_ns);
    if (missed_deadline) {
      misses_.fetch_add(1, std::memory_order_relaxed);
    }
    if (overran_budget) {
      overruns_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void record_missed_activations(std::uint64_t count) noexcept {
    // A skipped activation is a deadline miss by definition: the job did
    // not run, so it certainly did not finish in time.
    misses_.fetch_add(count, std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t deadline_misses() const noexcept {
    return misses_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t activations() const noexcept {
    return activations_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t budget_overruns() const noexcept {
    return overruns_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] TaskStatsSnapshot snapshot() const noexcept {
    TaskStatsSnapshot out;
    out.activations = activations_.load(std::memory_order_relaxed);
    out.deadline_misses = misses_.load(std::memory_order_relaxed);
    out.budget_overruns = overruns_.load(std::memory_order_relaxed);
    out.skipped_activations = skipped_.load(std::memory_order_relaxed);
    out.jitter_p50_ns = jitter_.percentile(kMedianQuantile);
    out.jitter_p99_ns = jitter_.percentile(kTailQuantile);
    out.jitter_max_ns = jitter_.max();
    out.response_p50_ns = response_.percentile(kMedianQuantile);
    out.response_p99_ns = response_.percentile(kTailQuantile);
    out.response_max_ns = response_.max();
    out.execution_p50_ns = execution_.percentile(kMedianQuantile);
    out.execution_p99_ns = execution_.percentile(kTailQuantile);
    out.execution_max_ns = execution_.max();
    return out;
  }

private:
  std::atomic<std::uint64_t> activations_{};
  std::atomic<std::uint64_t> misses_{};
  std::atomic<std::uint64_t> overruns_{};
  std::atomic<std::uint64_t> skipped_{};
  HdrHistogram jitter_;
  HdrHistogram response_;
  HdrHistogram execution_;
};

} // namespace volt::sched
