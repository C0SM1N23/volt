#include "volt/memory/allocation_metrics.hpp"

#include "volt/memory/allocation_tracker.hpp"

namespace volt::memory {

core::expected<void> AllocationMetrics::register_with(metrics::MetricRegistry &registry) noexcept {
  VOLT_TRY(registry.add(allocations_));
  VOLT_TRY(registry.add(bytes_));
  VOLT_TRY(registry.add(violations_));
  VOLT_TRY(registry.add(live_bytes_));
  VOLT_TRY(registry.add(peak_bytes_));
  VOLT_TRY(registry.add(tracked_threads_));
  return {};
}

void AllocationMetrics::refresh() noexcept {
  const AllocationTracker &tracker = AllocationTracker::instance();
  const AllocationStats current = tracker.process_stats();

  allocations_.add(current.allocation_count - published_.allocation_count);
  bytes_.add(current.total_bytes - published_.total_bytes);
  violations_.add(current.violation_count - published_.violation_count);
  published_ = current;

  live_bytes_.set(static_cast<double>(current.live_bytes));
  peak_bytes_.set(static_cast<double>(current.peak_live_bytes));
  tracked_threads_.set(static_cast<double>(tracker.tracked_threads()));
}

bool AllocationMetrics::measurable() noexcept { return AllocationTracker::hooks_installed(); }

} // namespace volt::memory
