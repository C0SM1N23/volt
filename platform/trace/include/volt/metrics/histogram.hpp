#pragma once

#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/metric.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metric_visitor.hpp"

#include "volt/core/error.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace volt::metrics {

namespace detail {

/// Returns ten raised to `exponent`.
[[nodiscard]] constexpr std::uint64_t power_of_ten(unsigned exponent) noexcept {
  std::uint64_t result = 1;
  for (unsigned step = 0; step < exponent; ++step) {
    result *= 10;
  }
  return result;
}

/// Returns the smallest magnitude whose power of two reaches `value`.
[[nodiscard]] constexpr unsigned ceil_log2(std::uint64_t value) noexcept {
  unsigned magnitude = 0;
  while ((std::uint64_t{1} << magnitude) < value) {
    ++magnitude;
  }
  return magnitude;
}

/// Returns how many powers-of-two magnitudes are needed to reach `max_value`.
///
/// From HdrHistogram: the first magnitude covers `sub_bucket_count` values at
/// unit resolution, and each further one doubles both the range it covers and
/// the width of its slots, which is what keeps the relative error constant.
[[nodiscard]] constexpr std::uint32_t magnitude_count(std::uint64_t max_value,
                                                      std::uint64_t sub_bucket_count) noexcept {
  std::uint32_t count = 1;
  std::uint64_t smallest_untrackable = sub_bucket_count;
  while (smallest_untrackable <= max_value) {
    smallest_untrackable <<= 1U;
    ++count;
  }
  return count;
}

} // namespace detail

/// A distribution recorded at constant relative error, for correct percentiles.
///
/// The layout is HdrHistogram's (Tene, HdrHistogram.org): values are bucketed
/// by their power-of-two magnitude, and each magnitude is cut into the same
/// number of equal slots. A slot near the top of the range is therefore wider
/// in absolute terms and identical in relative terms, which is why a 99.9th
/// percentile stays accurate without storing every sample.
///
/// Sized entirely at compile time, so recording touches one preallocated slot
/// and never allocates. `MaxValue` and `SignificantDigits` together decide that
/// size: raising either costs memory in proportion.
///
/// One writer, as AGENTS.md 6.1 asks: the task that measures a quantity is the
/// task that records it. Readers may be many and never block the writer.
///
/// @thread one writer calls `record`; any number of threads read
/// @rt     allocation-free and wait-free
template <std::uint64_t MaxValue, unsigned SignificantDigits = 3>
class Histogram final : public IMetric {
  static_assert(MaxValue >= 2, "a histogram needs a range to divide");
  static_assert(SignificantDigits >= 1 && SignificantDigits <= 5,
                "outside this range the bucket table is either useless or enormous");

  // Twice ten-to-the-digits is HdrHistogram's rule for how many slots one
  // magnitude needs to resolve that many significant digits. Three digits
  // means a relative error under 0.1%, which is what keeps the percentile
  // comparison in the tests inside its 1% budget.
  static constexpr std::uint64_t kUnitResolutionValue = 2 * detail::power_of_ten(SignificantDigits);
  static constexpr unsigned kSlotCountMagnitude = detail::ceil_log2(kUnitResolutionValue);
  static constexpr unsigned kHalfSlotCountMagnitude = kSlotCountMagnitude - 1;
  static constexpr std::uint64_t kSlotCount = std::uint64_t{1} << kSlotCountMagnitude;
  static constexpr std::uint64_t kHalfSlotCount = kSlotCount / 2;
  static constexpr std::uint64_t kSlotMask = kSlotCount - 1;
  static constexpr std::uint32_t kMagnitudeCount = detail::magnitude_count(MaxValue, kSlotCount);

public:
  /// How many counters the table holds. Public so a caller can weigh the cost.
  static constexpr std::size_t kBucketCount =
      static_cast<std::size_t>(kMagnitudeCount + 1) * kHalfSlotCount;

  /// Builds an empty histogram.
  /// @pre `spec` and everything it points at outlive this histogram
  explicit Histogram(const MetricSpec &spec) noexcept : spec_{spec} {}

  /// Files one observation.
  ///
  /// Relaxed throughout: the counters order nothing but themselves, and a
  /// reader that sees a count land one observation later still sees a
  /// distribution that was real.
  ///
  /// @post   the observation is included in every later snapshot
  /// @thread the owning writer
  /// @rt     allocation-free and wait-free
  /// @errors kInternalOutOfRange when `value` is above MaxValue, which no
  ///         bucket covers; the count of those is reported in the snapshot
  [[nodiscard]] core::expected<void> record(std::uint64_t value) noexcept {
    if (value > MaxValue) {
      store_relaxed(out_of_range_count_, out_of_range_count_.load(std::memory_order_relaxed) + 1);
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }

    const std::size_t index = bucket_of(value);
    VOLT_ASSERT(index < kBucketCount, "a value inside the range landed outside the table");
    store_relaxed(buckets_[index], buckets_[index].load(std::memory_order_relaxed) + 1);

    const std::uint64_t count = total_count_.load(std::memory_order_relaxed);
    store_relaxed(total_count_, count + 1);
    store_relaxed(sum_, sum_.load(std::memory_order_relaxed) + value);
    if (count == 0 || value < min_.load(std::memory_order_relaxed)) {
      store_relaxed(min_, value);
    }
    if (value > max_.load(std::memory_order_relaxed)) {
      store_relaxed(max_, value);
    }
    return {};
  }

  /// Returns the value at `phi`, where 0.99 asks for the 99th percentile.
  ///
  /// The answer is the top of the slot the quantile falls in, so it is never
  /// below the true value and never further above it than the histogram's
  /// relative error.
  ///
  /// @pre    `phi` is between 0 and 1; values outside are clamped
  /// @post   returns 0 when nothing has been recorded
  /// @thread any
  /// @rt     walks the table; for a scrape, not for the control loop
  [[nodiscard]] std::uint64_t value_at_quantile(double phi) const noexcept {
    const std::uint64_t total = total_count_.load(std::memory_order_relaxed);
    if (total == 0) {
      return 0;
    }

    std::uint64_t running = 0;
    const std::uint64_t target = rank_of(phi, total);
    VOLT_LOOP_BOUND(kBucketCount);
    for (std::size_t index = 0; index < kBucketCount; ++index) {
      running += buckets_[index].load(std::memory_order_relaxed);
      if (running >= target) {
        return highest_equivalent_value(index);
      }
    }
    // The walk above always reaches the rank, for the reason `fill_quantiles`
    // gives; this is the exit the language requires, not a fallback.
    return max_.load(std::memory_order_relaxed);
  }

  /// Returns how many observations were filed.
  [[nodiscard]] std::uint64_t count() const noexcept {
    return total_count_.load(std::memory_order_relaxed);
  }

  /// Returns one coherent picture of the distribution.
  ///
  /// Every reported quantile comes out of a single walk of the table, because
  /// the quantiles are in increasing order and so are the buckets.
  ///
  /// @thread any
  /// @rt     walks the table once; for a scrape, not for the control loop
  [[nodiscard]] HistogramSnapshot snapshot() const noexcept {
    HistogramSnapshot result;
    result.count = total_count_.load(std::memory_order_relaxed);
    result.sum = sum_.load(std::memory_order_relaxed);
    result.min = result.count == 0 ? 0 : min_.load(std::memory_order_relaxed);
    result.max = max_.load(std::memory_order_relaxed);
    result.out_of_range_count = out_of_range_count_.load(std::memory_order_relaxed);
    fill_quantiles(result);
    return result;
  }

  [[nodiscard]] const MetricSpec &spec() const noexcept override { return spec_; }

  void accept(IMetricVisitor &visitor) const noexcept override {
    visitor.visit_histogram(spec_, snapshot());
  }

private:
  /// Stores through a named helper so every relaxed write reads the same way.
  static void store_relaxed(std::atomic<std::uint64_t> &target, std::uint64_t value) noexcept {
    target.store(value, std::memory_order_relaxed);
  }

  /// Returns `phi` brought inside the zero-to-one range a quantile lives in.
  [[nodiscard]] static double clamp_quantile(double phi) noexcept {
    if (phi < 0.0) {
      return 0.0;
    }
    return phi > 1.0 ? 1.0 : phi;
  }

  /// Returns the rank the quantile asks for, counting from one.
  [[nodiscard]] static std::uint64_t rank_of(double phi, std::uint64_t total) noexcept {
    const double clamped = clamp_quantile(phi);
    // Rounded to nearest, so that phi and total agree on which observation is
    // being asked for; a truncation here shifts every quantile down by one.
    const double rank = (clamped * static_cast<double>(total)) + 0.5;
    const std::uint64_t target = static_cast<std::uint64_t>(rank);
    return target == 0 ? 1 : target;
  }

  /// Returns the table slot `value` belongs in.
  [[nodiscard]] static std::size_t bucket_of(std::uint64_t value) noexcept {
    const unsigned magnitude =
        static_cast<unsigned>(std::bit_width(value | kSlotMask)) - kSlotCountMagnitude;
    const std::uint64_t slot = value >> magnitude;
    const std::size_t magnitude_base = static_cast<std::size_t>(magnitude + 1)
                                       << kHalfSlotCountMagnitude;
    return magnitude_base + static_cast<std::size_t>(slot - kHalfSlotCount);
  }

  /// Returns the lowest value that lands in slot `index`.
  [[nodiscard]] static std::uint64_t value_at(std::size_t index) noexcept {
    const std::uint64_t magnitude_position = index >> kHalfSlotCountMagnitude;
    const std::uint64_t slot = (index & (kHalfSlotCount - 1)) + kHalfSlotCount;
    if (magnitude_position == 0) {
      return slot - kHalfSlotCount;
    }
    return slot << (magnitude_position - 1);
  }

  /// Returns the largest value that lands in slot `index`.
  [[nodiscard]] static std::uint64_t highest_equivalent_value(std::size_t index) noexcept {
    const std::uint64_t magnitude_position = index >> kHalfSlotCountMagnitude;
    const std::uint64_t width = std::uint64_t{1}
                                << (magnitude_position == 0 ? 0 : magnitude_position - 1);
    return value_at(index) + width - 1;
  }

  /// Walks the table once, recording every quantile as its rank is passed.
  void fill_quantiles(HistogramSnapshot &result) const noexcept {
    for (std::size_t quantile = 0; quantile < kReportedQuantiles.size(); ++quantile) {
      result.quantiles[quantile].phi = kReportedQuantiles[quantile];
    }
    if (result.count == 0) {
      return;
    }

    std::size_t next_quantile = 0;
    std::uint64_t running = 0;
    VOLT_LOOP_BOUND(kBucketCount);
    for (std::size_t index = 0; index < kBucketCount; ++index) {
      running += buckets_[index].load(std::memory_order_relaxed);
      next_quantile = settle_quantiles(result, next_quantile, running, index);
      if (next_quantile == kReportedQuantiles.size()) {
        return;
      }
    }

    // Unreachable rather than defended against: `record` fills a bucket before
    // it raises the total, so a reader that took the total first can only find
    // the buckets holding at least that many observations, and the walk above
    // therefore passes every rank it is looking for.
    VOLT_ASSERT(next_quantile == kReportedQuantiles.size(),
                "the bucket table held fewer observations than the total says");
  }

  /// Assigns every quantile whose rank the running total has now reached.
  [[nodiscard]] std::size_t settle_quantiles(HistogramSnapshot &result, std::size_t next_quantile,
                                             std::uint64_t running,
                                             std::size_t index) const noexcept {
    std::size_t quantile = next_quantile;
    VOLT_LOOP_BOUND(kReportedQuantiles.size());
    while (quantile < kReportedQuantiles.size() &&
           running >= rank_of(kReportedQuantiles[quantile], result.count)) {
      result.quantiles[quantile].value = highest_equivalent_value(index);
      ++quantile;
    }
    return quantile;
  }

  MetricSpec spec_;
  std::array<std::atomic<std::uint64_t>, kBucketCount> buckets_{};
  std::atomic<std::uint64_t> total_count_{0};
  std::atomic<std::uint64_t> sum_{0};
  std::atomic<std::uint64_t> min_{0};
  std::atomic<std::uint64_t> max_{0};
  std::atomic<std::uint64_t> out_of_range_count_{0};
};

} // namespace volt::metrics
