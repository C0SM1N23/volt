#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace volt::metrics {

/// The quantiles every histogram reports.
///
/// Median for the typical case, then the three tails a real-time system is
/// actually judged on (SPEC 25). Adding one costs a line per histogram in
/// every scrape, so the list is short on purpose.
inline constexpr std::array<double, 4> kReportedQuantiles{0.5, 0.9, 0.99, 0.999};

/// What a histogram looked like at one instant.
///
/// A copy rather than a live view: the counts keep moving while an exporter
/// formats them, and a scrape has to report one coherent picture.
struct HistogramSnapshot final {
  /// One reported quantile and the value at it.
  struct Quantile final {
    double phi = 0.0;
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool operator==(const Quantile &) const noexcept = default;
  };

  std::uint64_t count = 0;
  std::uint64_t sum = 0;
  /// Zero when nothing was recorded, where a minimum has no meaning.
  std::uint64_t min = 0;
  std::uint64_t max = 0;
  /// Values recorded above the histogram's range, which no bucket could hold.
  std::uint64_t out_of_range_count = 0;
  std::array<Quantile, kReportedQuantiles.size()> quantiles{};

  [[nodiscard]] constexpr bool operator==(const HistogramSnapshot &) const noexcept = default;
};

} // namespace volt::metrics
