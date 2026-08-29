#pragma once

#include "volt/metrics/label.hpp"

#include <span>
#include <string_view>

namespace volt::metrics {

/// What a metric is called and what it means.
///
/// The unit belongs in the name, as it does everywhere else in VOLT
/// (AGENTS.md 7.13) and as the Prometheus convention requires:
/// `volt_task_jitter_us`, `volt_alloc_violations_total`.
///
/// Every view here names storage the caller owns and must outlive the registry
/// the metric is added to; a scrape reads them again minutes later. String
/// literals are the intended source.
struct MetricSpec final {
  std::string_view name;
  /// One sentence for the `# HELP` line. Never empty: a metric nobody can
  /// interpret is a number nobody should act on.
  std::string_view help;
  std::span<const Label> labels;
};

} // namespace volt::metrics
