#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace volt::sched {

/// Latency histogram with bounded relative error, HdrHistogram-style: one
/// row of sixteen linear sub-buckets per power of two, so any recorded value
/// lands within 1/16 of its bucket's upper bound. SPEC 9.4 wants percentiles
/// per task at 1 kHz for hours, which rules out storing samples; 8 KiB of
/// counters per histogram is the whole cost, forever.
///
/// Buckets are relaxed atomics: the owning task records wait-free on the hot
/// path while a reporter sums concurrently. A snapshot taken mid-record can
/// be short by the samples still in flight, never torn or wrong beyond that,
/// and a percentile over millions of samples does not care about the last
/// handful.
///
/// @thread one writer records; any reader queries
/// @rt     record is allocation-free, lock-free, O(1)
class HdrHistogram final {
public:
  /// Adds one value.
  void record(std::uint64_t value) noexcept {
    // Relaxed: buckets order nothing; the counts only have to be complete
    // by the time a report is taken, which quiescence provides.
    buckets_[index_of(value)].fetch_add(1, std::memory_order_relaxed);
    std::uint64_t seen = max_.load(std::memory_order_relaxed);
    while (value > seen &&
           // Relaxed for the same reason; the loop retries on a concurrent
           // larger maximum and stops as soon as it is no longer the record.
           !max_.compare_exchange_weak(seen, value, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
    }
    total_.fetch_add(1, std::memory_order_relaxed);
  }

  /// Returns how many values were recorded.
  [[nodiscard]] std::uint64_t total() const noexcept {
    return total_.load(std::memory_order_relaxed);
  }

  /// Returns the exact largest recorded value, zero when empty.
  [[nodiscard]] std::uint64_t max() const noexcept { return max_.load(std::memory_order_relaxed); }

  /// Returns an upper bound of the `quantile` percentile, exact to within
  /// one sixteenth. `quantile` in [0, 1].
  [[nodiscard]] std::uint64_t percentile(double quantile) const noexcept;

private:
  /// Sixteen linear sub-buckets per magnitude keep the relative error at
  /// 1/16 for any value; below sixteen every value has its own bucket.
  static constexpr std::uint64_t kSubBuckets = 16;
  static constexpr unsigned kSubBucketBits = 4;
  static constexpr std::size_t kMagnitudes = 61;
  static constexpr std::size_t kBucketCount = kSubBuckets * (kMagnitudes + 1);

  [[nodiscard]] static constexpr std::size_t index_of(std::uint64_t value) noexcept {
    if (value < kSubBuckets) {
      return static_cast<std::size_t>(value);
    }
    const unsigned magnitude = static_cast<unsigned>(std::bit_width(value)) - 1U;
    const std::uint64_t sub = (value >> (magnitude - kSubBucketBits)) & (kSubBuckets - 1U);
    return (static_cast<std::size_t>(magnitude - kSubBucketBits + 1U) * kSubBuckets) +
           static_cast<std::size_t>(sub);
  }

  /// Largest value that lands in bucket `index`, the bound `percentile`
  /// reports.
  [[nodiscard]] static constexpr std::uint64_t upper_bound_of(std::size_t index) noexcept {
    if (index < kSubBuckets) {
      return index;
    }
    const std::uint64_t magnitude = (index / kSubBuckets) + kSubBucketBits - 1U;
    const std::uint64_t sub = index % kSubBuckets;
    return ((kSubBuckets + sub + 1U) << (magnitude - kSubBucketBits)) - 1U;
  }

  std::array<std::atomic<std::uint64_t>, kBucketCount> buckets_{};
  std::atomic<std::uint64_t> total_{};
  std::atomic<std::uint64_t> max_{};
};

} // namespace volt::sched
