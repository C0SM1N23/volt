#pragma once

#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/metric_spec.hpp"

#include <cstdint>

namespace volt::metrics {

/// What an exporter is told about each metric in a registry.
///
/// A visitor rather than a kind tag and a downcast: the three metric shapes
/// carry different data, and this way adding a shape breaks every exporter at
/// compile time instead of silently exporting nothing for it.
class IMetricVisitor {
public:
  IMetricVisitor() = default;
  virtual ~IMetricVisitor() = default;

  IMetricVisitor(const IMetricVisitor &) = delete;
  IMetricVisitor &operator=(const IMetricVisitor &) = delete;
  IMetricVisitor(IMetricVisitor &&) = delete;
  IMetricVisitor &operator=(IMetricVisitor &&) = delete;

  /// Reports a monotonically increasing total.
  virtual void visit_counter(const MetricSpec &spec, std::uint64_t value) noexcept = 0;

  /// Reports a value that moves in both directions.
  virtual void visit_gauge(const MetricSpec &spec, double value) noexcept = 0;

  /// Reports a distribution as of one instant.
  virtual void visit_histogram(const MetricSpec &spec,
                               const HistogramSnapshot &snapshot) noexcept = 0;
};

} // namespace volt::metrics
