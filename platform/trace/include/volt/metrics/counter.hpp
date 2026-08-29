#pragma once

#include "volt/metrics/metric.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include <atomic>
#include <cstdint>

namespace volt::metrics {

/// A total that only ever grows: events seen, errors counted, bytes sent.
///
/// Any thread may add to it. Every access is relaxed, because a counter orders
/// nothing but itself and a scrape that reads a total a few events late is
/// still reading a total that was true.
///
/// @thread any thread may record; any thread may read
/// @rt     allocation-free and wait-free
class Counter final : public IMetric {
public:
  /// Builds a counter at zero.
  /// @pre `spec` and everything it points at outlive this counter
  explicit Counter(const MetricSpec &spec) noexcept : spec_{spec} {}

  /// Adds one.
  /// @thread any
  /// @rt     allocation-free and wait-free
  void increment() noexcept { value_.fetch_add(1, std::memory_order_relaxed); }

  /// Adds `amount`.
  ///
  /// Unsigned because a counter that can go down is a gauge, and a scraper
  /// reads a decrease as a process restart.
  ///
  /// @thread any
  /// @rt     allocation-free and wait-free
  void add(std::uint64_t amount) noexcept { value_.fetch_add(amount, std::memory_order_relaxed); }

  /// Returns the total so far.
  [[nodiscard]] std::uint64_t value() const noexcept {
    return value_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] const MetricSpec &spec() const noexcept override { return spec_; }

  void accept(IMetricVisitor &visitor) const noexcept override {
    visitor.visit_counter(spec_, value());
  }

private:
  MetricSpec spec_;
  std::atomic<std::uint64_t> value_{0};
};

} // namespace volt::metrics
