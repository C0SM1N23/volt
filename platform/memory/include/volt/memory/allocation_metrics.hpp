#pragma once

#include "volt/memory/allocation_stats.hpp"

#include "volt/core/error.hpp"
#include "volt/metrics/counter.hpp"
#include "volt/metrics/gauge.hpp"
#include "volt/metrics/metric_registry.hpp"

namespace volt::memory {

/// Publishes what the allocation tracker counts, as metrics a scrape can read.
///
/// The counters live in the tracker and are updated on the allocation path,
/// where nothing may be published; this object is what the control plane calls
/// to move them into the registry. `refresh()` is the whole of that work, and
/// it belongs on the same thread that scrapes.
///
/// `volt_alloc_violations_total` is one of the metrics SPEC 22.4 names by hand,
/// and it is the number that says whether K10 held.
///
/// @thread one thread calls refresh; the registry may be scraped from another
/// @rt     never call it from the control loop
class AllocationMetrics final {
public:
  /// Builds the metrics without publishing them yet.
  AllocationMetrics() noexcept = default;

  /// Adds every metric to `registry`.
  ///
  /// @pre    this object outlives `registry`, which keeps references to it
  /// @thread startup
  /// @errors whatever the registry reports for a duplicate or a full table
  [[nodiscard]] core::expected<void> register_with(metrics::MetricRegistry &registry) noexcept;

  /// Copies the tracker's current totals into the metrics.
  ///
  /// Counters move by the difference since the last call rather than being
  /// assigned, because a counter that can be set to a smaller number is read
  /// by a scraper as a process that restarted.
  ///
  /// @thread the scraping thread
  void refresh() noexcept;

  /// Reports whether the numbers mean anything in this build.
  ///
  /// False when a sanitizer runtime owns the allocation operators, which
  /// leaves every counter at zero for a reason that has nothing to do with how
  /// much the program allocated. A dashboard showing zero should say which of
  /// the two it is looking at.
  [[nodiscard]] static bool measurable() noexcept;

private:
  AllocationStats published_{};

  metrics::Counter allocations_{
      metrics::MetricSpec{.name = "volt_alloc_allocations_total",
                          .help = "Allocations made since the process started.",
                          .labels = {}}};
  metrics::Counter bytes_{metrics::MetricSpec{
      .name = "volt_alloc_bytes_total",
      .help = "Bytes handed out since the process started, live or already released.",
      .labels = {}}};
  metrics::Counter violations_{metrics::MetricSpec{
      .name = "volt_alloc_violations_total",
      .help = "Allocations made inside a no_alloc_scope, which K10 requires to stay at zero.",
      .labels = {}}};
  metrics::Gauge live_bytes_{metrics::MetricSpec{.name = "volt_alloc_live_bytes",
                                                 .help = "Bytes allocated and not yet released.",
                                                 .labels = {}}};
  metrics::Gauge peak_bytes_{
      metrics::MetricSpec{.name = "volt_alloc_peak_bytes",
                          .help = "The high-water mark of bytes held at one time.",
                          .labels = {}}};
  metrics::Gauge tracked_threads_{
      metrics::MetricSpec{.name = "volt_alloc_tracked_threads",
                          .help = "Threads that claimed a slot in the allocation tracker.",
                          .labels = {}}};
};

} // namespace volt::memory
