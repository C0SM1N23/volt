#pragma once

#include "volt/metrics/exposition_writer.hpp"
#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/label.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {

/// Writes a registry as the plain text a person reads in a terminal.
///
/// The second of the two formats SPEC 22.4 asks for. Where the Prometheus
/// exposition is written for a scraper and repeats the name on every line, this
/// one is written for whoever is looking at a node right now: one line per
/// metric, the distribution summarised in place.
///
/// @thread one exporter per dump; not shared
/// @rt     allocation-free; control plane only
class TextExporter final : public IMetricVisitor {
public:
  /// Writes the dump into `out`, which the caller owns.
  explicit TextExporter(std::span<char> out) noexcept : writer_{out} {}

  void visit_counter(const MetricSpec &spec, std::uint64_t value) noexcept override;
  void visit_gauge(const MetricSpec &spec, double value) noexcept override;
  void visit_histogram(const MetricSpec &spec, const HistogramSnapshot &snapshot) noexcept override;

  /// Returns the dump written so far.
  [[nodiscard]] std::string_view view() const noexcept { return writer_.view(); }

  /// Returns how many bytes were written.
  [[nodiscard]] std::size_t written() const noexcept { return writer_.written(); }

  /// Reports whether the buffer ran out, which makes the dump incomplete.
  [[nodiscard]] bool truncated() const noexcept { return writer_.truncated(); }

private:
  /// Writes `name` and its labels, then pads to the value column.
  void write_heading(std::string_view name, std::span<const Label> labels) noexcept;

  ExpositionWriter writer_;
};

} // namespace volt::metrics
