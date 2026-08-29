#include "volt/metrics/prometheus_exporter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {
namespace {

constexpr std::string_view kCounterType = "counter";
constexpr std::string_view kGaugeType = "gauge";
constexpr std::string_view kSummaryType = "summary";

// The label a summary uses to say which quantile a line reports, and the
// suffixes the format reserves for the rest of one. All fixed by the
// exposition format, not chosen here.
constexpr std::string_view kQuantileLabel = "quantile";
constexpr std::string_view kSumSuffix = "_sum";
constexpr std::string_view kCountSuffix = "_count";

// Observations a histogram could not file because they were above its range.
// Their own family rather than a line inside the summary, because the format
// has no place for them there and AGENTS.md 4.5 has no place for an error that
// goes uncounted.
constexpr std::string_view kOutOfRangeSuffix = "_out_of_range_total";
constexpr std::string_view kOutOfRangeHelp =
    "Observations above the range of the histogram of the same name.";

// Room for one quantile written as text. The four reported quantiles are all
// short, and the buffer is only ever handed to an append that happens before
// it goes out of scope.
constexpr std::size_t kQuantileTextBytes = 16;

} // namespace

bool PrometheusExporter::remember_family(std::string_view name) noexcept {
  const std::span<const std::string_view> announced{families_.data(), family_count_};
  if (std::ranges::find(announced, name) != announced.end()) {
    return false;
  }
  if (family_count_ == families_.size()) {
    families_overflowed_ = true;
    return true;
  }
  families_[family_count_] = name;
  ++family_count_;
  return true;
}

void PrometheusExporter::announce(const MetricSpec &spec, std::string_view type) noexcept {
  if (!remember_family(spec.name)) {
    return;
  }
  writer_.append("# HELP ");
  writer_.append(spec.name);
  writer_.append(" ");
  writer_.append_help(spec.help);
  writer_.append("\n# TYPE ");
  writer_.append(spec.name);
  writer_.append(" ");
  writer_.append(type);
  writer_.append("\n");
}

void PrometheusExporter::announce_histogram(const MetricSpec &spec) noexcept {
  if (!remember_family(spec.name)) {
    return;
  }
  writer_.append("# HELP ");
  writer_.append(spec.name);
  writer_.append(" ");
  writer_.append_help(spec.help);
  writer_.append("\n# TYPE ");
  writer_.append(spec.name);
  writer_.append(" ");
  writer_.append(kSummaryType);
  writer_.append("\n# HELP ");
  writer_.append(spec.name);
  writer_.append(kOutOfRangeSuffix);
  writer_.append(" ");
  writer_.append_help(kOutOfRangeHelp);
  writer_.append("\n# TYPE ");
  writer_.append(spec.name);
  writer_.append(kOutOfRangeSuffix);
  writer_.append(" ");
  writer_.append(kCounterType);
  writer_.append("\n");
}

void PrometheusExporter::write_series(std::string_view name, std::string_view suffix,
                                      std::span<const Label> labels,
                                      std::string_view quantile) noexcept {
  writer_.append(name);
  writer_.append(suffix);
  if (labels.empty() && quantile.empty()) {
    return;
  }

  writer_.append("{");
  bool first = true;
  for (const Label &label : labels) {
    if (!first) {
      writer_.append(",");
    }
    first = false;
    writer_.append(label.name);
    writer_.append("=\"");
    writer_.append_label_value(label.value);
    writer_.append("\"");
  }
  if (!quantile.empty()) {
    if (!first) {
      writer_.append(",");
    }
    writer_.append(kQuantileLabel);
    writer_.append("=\"");
    writer_.append(quantile);
    writer_.append("\"");
  }
  writer_.append("}");
}

void PrometheusExporter::write_sample(std::string_view name, std::string_view suffix,
                                      std::span<const Label> labels, std::uint64_t value) noexcept {
  write_series(name, suffix, labels, {});
  writer_.append(" ");
  writer_.append(value);
  writer_.append("\n");
}

void PrometheusExporter::visit_counter(const MetricSpec &spec, std::uint64_t value) noexcept {
  announce(spec, kCounterType);
  write_sample(spec.name, {}, spec.labels, value);
}

void PrometheusExporter::visit_gauge(const MetricSpec &spec, double value) noexcept {
  announce(spec, kGaugeType);
  write_series(spec.name, {}, spec.labels, {});
  writer_.append(" ");
  writer_.append(value);
  writer_.append("\n");
}

void PrometheusExporter::visit_histogram(const MetricSpec &spec,
                                         const HistogramSnapshot &snapshot) noexcept {
  announce_histogram(spec);

  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    std::array<char, kQuantileTextBytes> quantile_text{};
    ExpositionWriter quantile_writer{quantile_text};
    quantile_writer.append(quantile.phi);
    write_series(spec.name, {}, spec.labels, quantile_writer.view());
    writer_.append(" ");
    writer_.append(quantile.value);
    writer_.append("\n");
  }

  write_sample(spec.name, kSumSuffix, spec.labels, snapshot.sum);
  write_sample(spec.name, kCountSuffix, spec.labels, snapshot.count);
  write_sample(spec.name, kOutOfRangeSuffix, spec.labels, snapshot.out_of_range_count);
}

} // namespace volt::metrics
