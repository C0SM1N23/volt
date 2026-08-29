#include "volt/metrics/text_exporter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {
namespace {

// Where the value column starts. Wide enough for the metric names in SPEC 22.4
// with a label or two; a longer heading simply pushes its own value right.
constexpr std::size_t kValueColumn = 44;

// One space at a time, so a heading that overruns the column still ends with a
// separator instead of running into its value.
constexpr std::string_view kSpace = " ";

} // namespace

void TextExporter::write_heading(std::string_view name, std::span<const Label> labels) noexcept {
  const std::size_t start = writer_.written();
  writer_.append(name);
  for (const Label &label : labels) {
    writer_.append(" ");
    writer_.append(label.name);
    writer_.append("=");
    writer_.append(label.value);
  }

  const std::size_t heading_width = writer_.written() - start;
  const std::size_t padding = heading_width >= kValueColumn ? 1 : kValueColumn - heading_width;
  for (std::size_t column = 0; column < padding; ++column) {
    writer_.append(kSpace);
  }
}

void TextExporter::visit_counter(const MetricSpec &spec, std::uint64_t value) noexcept {
  write_heading(spec.name, spec.labels);
  writer_.append(value);
  writer_.append("\n");
}

void TextExporter::visit_gauge(const MetricSpec &spec, double value) noexcept {
  write_heading(spec.name, spec.labels);
  writer_.append(value);
  writer_.append("\n");
}

void TextExporter::visit_histogram(const MetricSpec &spec,
                                   const HistogramSnapshot &snapshot) noexcept {
  write_heading(spec.name, spec.labels);
  writer_.append("count=");
  writer_.append(snapshot.count);
  writer_.append(" min=");
  writer_.append(snapshot.min);
  writer_.append(" max=");
  writer_.append(snapshot.max);
  writer_.append(" sum=");
  writer_.append(snapshot.sum);
  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    writer_.append(" p");
    writer_.append(quantile.phi);
    writer_.append("=");
    writer_.append(quantile.value);
  }
  writer_.append(" out_of_range=");
  writer_.append(snapshot.out_of_range_count);
  writer_.append("\n");
}

} // namespace volt::metrics
