#pragma once

#include "volt/metrics/exposition_writer.hpp"
#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/label.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {

/// Most distinct metric names one exposition may carry.
///
/// A name gets one `# HELP` and one `# TYPE` line however many label sets it
/// has, so the exporter has to remember which names it already announced.
/// Matches the registry's own capacity, so a registry that fits cannot make
/// this overflow.
inline constexpr std::size_t kMaxFamilies = 256;

/// Writes a registry in the Prometheus text exposition format, version 0.0.4.
///
/// Histograms leave as summaries rather than as Prometheus histograms: the
/// bucket table behind them has thousands of slots, and shipping those on
/// every scrape would cost far more than the four quantiles anyone reads. A
/// summary carries precomputed quantiles, which is exactly what an HDR
/// histogram is good at.
///
/// `# HELP` and `# TYPE` are written the first time a name is seen, because
/// the format allows one of each per metric family while a family may carry
/// many label sets.
///
/// @thread one exporter per scrape; not shared
/// @rt     allocation-free; control plane only
class PrometheusExporter final : public IMetricVisitor {
public:
  /// Writes the exposition into `out`, which the caller owns.
  explicit PrometheusExporter(std::span<char> out) noexcept : writer_{out} {}

  void visit_counter(const MetricSpec &spec, std::uint64_t value) noexcept override;
  void visit_gauge(const MetricSpec &spec, double value) noexcept override;
  void visit_histogram(const MetricSpec &spec, const HistogramSnapshot &snapshot) noexcept override;

  /// Returns the exposition written so far.
  [[nodiscard]] std::string_view view() const noexcept { return writer_.view(); }

  /// Returns how many bytes were written.
  [[nodiscard]] std::size_t written() const noexcept { return writer_.written(); }

  /// Reports whether the buffer ran out, which makes the page incomplete.
  [[nodiscard]] bool truncated() const noexcept { return writer_.truncated(); }

  /// Reports whether a name was dropped because too many families appeared.
  [[nodiscard]] bool families_overflowed() const noexcept { return families_overflowed_; }

private:
  /// Writes the HELP and TYPE lines unless this name already has them.
  void announce(const MetricSpec &spec, std::string_view type) noexcept;

  /// Announces a summary and the counter that carries its out-of-range total.
  ///
  /// Both at once, keyed on the histogram's own name, because the derived name
  /// is composed straight into the output and never outlives the call.
  void announce_histogram(const MetricSpec &spec) noexcept;

  /// Writes `name` with an optional suffix, its labels, and an optional
  /// `quantile` label, leaving the cursor after the closing brace.
  void write_series(std::string_view name, std::string_view suffix, std::span<const Label> labels,
                    std::string_view quantile) noexcept;

  /// Writes one complete sample line, value and newline included.
  void write_sample(std::string_view name, std::string_view suffix, std::span<const Label> labels,
                    std::uint64_t value) noexcept;

  /// Records `name` as announced, reporting whether it was new.
  [[nodiscard]] bool remember_family(std::string_view name) noexcept;

  ExpositionWriter writer_;

  // One entry per metric family already announced. Sized to the registry, so
  // a registry that fits cannot overflow this; the flag exists for a caller
  // that drives the exporter by hand with more families than that.
  std::array<std::string_view, kMaxFamilies> families_{};
  std::size_t family_count_ = 0;
  bool families_overflowed_ = false;
};

} // namespace volt::metrics
