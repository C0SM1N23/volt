#pragma once

#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

namespace volt::metrics {

/// Anything a registry can hand to an exporter.
///
/// Metrics are owned by whoever reports them, not by the registry: the object
/// lives as a member of the service that updates it, and the registry holds a
/// reference. That keeps recording a direct call on a member the owner already
/// has, with no lookup on the data plane, and it is why a registered metric
/// must outlive the registry it was added to.
class IMetric {
public:
  IMetric() = default;
  virtual ~IMetric() = default;

  // Deleted because the registry holds a pointer to this exact object; a copy
  // would be a second metric answering to one name.
  IMetric(const IMetric &) = delete;
  IMetric &operator=(const IMetric &) = delete;
  IMetric(IMetric &&) = delete;
  IMetric &operator=(IMetric &&) = delete;

  /// Returns what this metric is called.
  [[nodiscard]] virtual const MetricSpec &spec() const noexcept = 0;

  /// Hands the current value to `visitor`.
  ///
  /// @thread any; a reader never blocks the thread that records
  /// @rt     allocation-free
  virtual void accept(IMetricVisitor &visitor) const noexcept = 0;
};

} // namespace volt::metrics
