#include "volt/metrics/metric_registry.hpp"

#include <algorithm>
#include <cstddef>

namespace volt::metrics {

MetricRegistry &MetricRegistry::instance() noexcept {
  // Function-local static: built on first use and destroyed after main, which
  // is later than any thread that could still be scraping.
  static MetricRegistry registry;
  return registry;
}

bool MetricRegistry::same_series(const MetricSpec &left, const MetricSpec &right) noexcept {
  return left.name == right.name && std::ranges::equal(left.labels, right.labels);
}

core::expected<void> MetricRegistry::add(const IMetric &metric) noexcept {
  if (find(metric.spec()) != nullptr) {
    return std::unexpected{core::ErrorCode::kConfigDuplicateId};
  }

  const std::size_t count = count_.load(std::memory_order_relaxed);
  if (count == kMaxMetrics) {
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }

  metrics_[count] = &metric;
  // Release pairs with the acquire in `collect` and `find`, so a scrape that
  // sees the new count also sees the pointer that count exposes.
  count_.store(count + 1, std::memory_order_release);
  return {};
}

void MetricRegistry::collect(IMetricVisitor &visitor) const noexcept {
  const std::size_t count = count_.load(std::memory_order_acquire);
  for (std::size_t index = 0; index < count; ++index) {
    metrics_[index]->accept(visitor);
  }
}

std::size_t MetricRegistry::size() const noexcept { return count_.load(std::memory_order_acquire); }

const IMetric *MetricRegistry::find(const MetricSpec &spec) const noexcept {
  const std::size_t count = count_.load(std::memory_order_acquire);
  for (std::size_t index = 0; index < count; ++index) {
    if (same_series(metrics_[index]->spec(), spec)) {
      return metrics_[index];
    }
  }
  return nullptr;
}

} // namespace volt::metrics
