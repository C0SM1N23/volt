#pragma once

#include "volt/metrics/metric.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include "volt/core/error.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace volt::metrics {

/// Most metrics one registry can hold.
///
/// A fixed array rather than a growing one, so adding a metric needs no lock
/// and a scrape's work is bounded (SPEC 5.5). Two hundred and fifty-six covers
/// the metric list in SPEC 22.4 many times over; a deployment that reaches it
/// should say so loudly rather than quietly reallocate under a scrape.
inline constexpr std::size_t kMaxMetrics = 256;

/// The set of metrics a scrape reports.
///
/// Registration belongs to startup and reading belongs to the control plane,
/// so the two never contend: entries are published with a release that the
/// reader's acquire pairs with, and nothing is ever removed.
///
/// Constructible rather than only global, deliberately: a test builds its own
/// registry and sees exactly what it put in, and the process-wide one is just
/// the instance the exporter reaches for by default.
class MetricRegistry final {
public:
  MetricRegistry() = default;

  ~MetricRegistry() = default;
  // Rule of five because a registered metric holds no back-pointer: copying a
  // registry would silently give one metric two homes and two scrape entries.
  MetricRegistry(const MetricRegistry &) = delete;
  MetricRegistry &operator=(const MetricRegistry &) = delete;
  MetricRegistry(MetricRegistry &&) = delete;
  MetricRegistry &operator=(MetricRegistry &&) = delete;

  /// Returns the process-wide registry.
  ///
  /// A singleton, which AGENTS.md 2.9 permits by name for exactly this: a
  /// scrape has to find every metric in the program without each one being
  /// threaded through the call graph.
  [[nodiscard]] static MetricRegistry &instance() noexcept;

  /// Adds `metric` to what a scrape reports.
  ///
  /// @pre    `metric` outlives this registry; the registry holds a reference
  /// @post   every later `collect` reports it
  /// @thread startup, before the scrape thread runs
  /// @rt     allocation-free
  /// @errors kConfigDuplicateId when a metric with the same name and labels is
  ///         already registered, kResourceExhausted when the registry is full
  [[nodiscard]] core::expected<void> add(const IMetric &metric) noexcept;

  /// Hands every registered metric to `visitor`, in registration order.
  ///
  /// @thread any; the metrics keep being updated while this runs
  /// @rt     walks every metric; for a scrape, not for the control loop
  void collect(IMetricVisitor &visitor) const noexcept;

  /// Returns how many metrics are registered.
  [[nodiscard]] std::size_t size() const noexcept;

  /// Returns the metric registered under `name` with `labels`, if any.
  ///
  /// @post returns nullptr when nothing matches
  [[nodiscard]] const IMetric *find(const MetricSpec &spec) const noexcept;

private:
  /// Reports whether two specs name the same series.
  [[nodiscard]] static bool same_series(const MetricSpec &left, const MetricSpec &right) noexcept;

  std::array<const IMetric *, kMaxMetrics> metrics_{};

  // Published with release so a metric is fully constructed before the count
  // that exposes it to a scrape running on another thread.
  std::atomic<std::size_t> count_{0};
};

} // namespace volt::metrics
