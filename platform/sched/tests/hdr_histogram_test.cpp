#include "volt/sched/hdr_histogram.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace volt::sched {
namespace {

/// SplitMix64, seeded constant so a failure replays (AGENTS.md 8.5).
class Rng {
public:
  std::uint64_t next() {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state_;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
  }

private:
  std::uint64_t state_ = 0x5EED0001;
};

TEST(HdrHistogramTest, SmallValuesAreExact) {
  HdrHistogram histogram;
  for (std::uint64_t value = 0; value < 16; ++value) {
    histogram.record(value);
  }
  EXPECT_EQ(histogram.total(), 16U);
  EXPECT_EQ(histogram.max(), 15U);
  EXPECT_EQ(histogram.percentile(0.0), 0U);
  EXPECT_EQ(histogram.percentile(1.0), 15U);
}

TEST(HdrHistogramTest, PercentilesMatchGroundTruthWithinResolution) {
  HdrHistogram histogram;
  Rng rng;
  std::vector<std::uint64_t> samples;
  // Values span nine orders of magnitude, like nanosecond latencies do.
  for (int index = 0; index < 100'000; ++index) {
    const std::uint64_t value = rng.next() % (1ULL << (10 + (index % 30)));
    samples.push_back(value);
    histogram.record(value);
  }
  std::ranges::sort(samples);

  for (const double quantile : {0.50, 0.90, 0.99, 0.999}) {
    const std::uint64_t truth =
        samples[static_cast<std::size_t>(quantile * static_cast<double>(samples.size() - 1))];
    const std::uint64_t reported = histogram.percentile(quantile);
    // The histogram reports its bucket's upper bound: never below the true
    // value's bucket, never more than one sixteenth above it.
    EXPECT_GE(reported, truth - (truth / 16) - 1) << "quantile " << quantile;
    EXPECT_LE(reported, truth + (truth / 8) + 1) << "quantile " << quantile;
  }
  EXPECT_EQ(histogram.max(), samples.back());
}

TEST(HdrHistogramTest, TailPercentileNeverExceedsTheRealMaximum) {
  HdrHistogram histogram;
  histogram.record(1'000'000);
  // One sample: every percentile is that sample, not its bucket's bound.
  EXPECT_EQ(histogram.percentile(0.999), 1'000'000U);
  EXPECT_EQ(histogram.percentile(0.5), 1'000'000U);
}

TEST(HdrHistogramTest, EmptyHistogramReportsZero) {
  const HdrHistogram histogram;
  EXPECT_EQ(histogram.total(), 0U);
  EXPECT_EQ(histogram.percentile(0.99), 0U);
  EXPECT_EQ(histogram.max(), 0U);
}

TEST(HdrHistogramTest, HugeValuesLandInTheLastBuckets) {
  HdrHistogram histogram;
  const std::uint64_t huge = ~0ULL - 5;
  histogram.record(huge);
  EXPECT_EQ(histogram.max(), huge);
  EXPECT_EQ(histogram.percentile(1.0), huge);
}

} // namespace
} // namespace volt::sched
