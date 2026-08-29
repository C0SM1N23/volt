#pragma once

#include "volt/metrics/metric.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include <atomic>

namespace volt::metrics {

/// A value that moves both ways: a temperature, a ratio, an epoch, a queue depth.
///
/// One writer, which AGENTS.md 6.1 asks of every piece of shared state: the
/// task that owns the quantity is the task that publishes it. Readers may be
/// many. That is what lets a write be a single store rather than a
/// read-modify-write whose retries would be unbounded on the data plane.
///
/// @thread one writer calls `set`; any number of threads read
/// @rt     allocation-free and wait-free
class Gauge final : public IMetric {
public:
  /// Builds a gauge at zero.
  /// @pre `spec` and everything it points at outlive this gauge
  explicit Gauge(const MetricSpec &spec) noexcept : spec_{spec} {}

  /// Publishes the current value.
  ///
  /// Relaxed: a gauge orders nothing but itself, and a scrape that reads a
  /// value one sample late reads a value that was true one sample ago.
  ///
  /// @thread the owning writer
  /// @rt     allocation-free and wait-free
  void set(double value) noexcept { value_.store(value, std::memory_order_relaxed); }

  /// Returns the value last published.
  [[nodiscard]] double value() const noexcept { return value_.load(std::memory_order_relaxed); }

  [[nodiscard]] const MetricSpec &spec() const noexcept override { return spec_; }

  void accept(IMetricVisitor &visitor) const noexcept override {
    visitor.visit_gauge(spec_, value());
  }

private:
  static_assert(std::atomic<double>::is_always_lock_free,
                "a gauge is read from the control plane while the data plane writes it");

  MetricSpec spec_;
  std::atomic<double> value_{0.0};
};

} // namespace volt::metrics
