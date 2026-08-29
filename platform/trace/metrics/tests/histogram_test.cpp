#include "volt/metrics/histogram.hpp"
#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/metric_spec.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string_view>
#include <vector>

namespace volt::metrics {
namespace {

// The range the sweep covers. Large enough to cross seven magnitude
// boundaries, which is where bucket arithmetic goes wrong if it is going to.
constexpr std::uint64_t kSweepRange = 100'000;
using SweepHistogram = Histogram<kSweepRange, 3>;

// The campaign P10 asks for: a million samples compared against an exact
// percentile of the same samples.
constexpr std::size_t kReferenceSampleCount = 1'000'000;
constexpr std::uint64_t kReferenceRange = 1'000'000;
using ReferenceHistogram = Histogram<kReferenceRange, 3>;

// Fixed seed required by AGENTS.md 8.5, reported on every failure so the exact
// run can be repeated.
constexpr std::uint64_t kSampleSeed = 0x51F3'C0DE'2A7B'9E14ULL;

// Most of the traffic is short and a twentieth of it is slow, which is the
// shape a real latency distribution has and the reason a tail quantile is
// worth measuring at all.
constexpr double kSlowSampleShare = 0.05;
constexpr std::uint64_t kFastSampleCeiling = 1'000;
constexpr std::uint64_t kSlowSampleCeiling = 500'000;

// What P10 allows between the histogram and an exact percentile. The layout's
// own guarantee at three significant digits is about a tenth of this; the
// budget is the prompt's, and the margin between the two is the point.
constexpr double kPermittedRelativeError = 0.01;

// What the bucket layout itself promises: a slot is one part in `kSlotCount`
// of its magnitude, so two values in one slot differ by under two parts in a
// thousand. A sweep that exceeds this has bucket arithmetic wrong, whatever
// the looser budget above would have allowed.
constexpr double kLayoutRelativeError = 0.002;

[[nodiscard]] MetricSpec spec_of(std::string_view name) {
  return MetricSpec{.name = name, .help = "A distribution under test.", .labels = {}};
}

/// Returns the rank a quantile asks for, the way the histogram counts it.
[[nodiscard]] std::size_t rank_of(double phi, std::size_t total) {
  const double rank = (phi * static_cast<double>(total)) + 0.5;
  const std::size_t target = static_cast<std::size_t>(rank);
  return target == 0 ? 1 : target;
}

/// Returns the exact value at `phi` in a sorted sample, by the same rank rule.
[[nodiscard]] std::uint64_t exact_quantile(const std::vector<std::uint64_t> &sorted, double phi) {
  return sorted[rank_of(phi, sorted.size()) - 1];
}

/// Returns how far `measured` is from `expected`, in parts of `expected`.
[[nodiscard]] double relative_error(std::uint64_t measured, std::uint64_t expected) {
  const double reference = expected == 0 ? 1.0 : static_cast<double>(expected);
  return std::abs(static_cast<double>(measured) - static_cast<double>(expected)) / reference;
}

TEST(HistogramTest, ReportsNothingBeforeAnythingIsRecorded) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_empty_us"));
  const HistogramSnapshot snapshot = histogram->snapshot();

  EXPECT_EQ(snapshot.count, 0U);
  EXPECT_EQ(snapshot.sum, 0U);
  EXPECT_EQ(snapshot.min, 0U);
  EXPECT_EQ(snapshot.max, 0U);
  EXPECT_EQ(histogram->value_at_quantile(0.5), 0U);
  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    EXPECT_EQ(quantile.value, 0U);
  }
}

TEST(HistogramTest, TracksCountSumMinimumAndMaximum) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_summary_us"));
  ASSERT_TRUE(histogram->record(70).has_value());
  ASSERT_TRUE(histogram->record(10).has_value());
  ASSERT_TRUE(histogram->record(40).has_value());

  const HistogramSnapshot snapshot = histogram->snapshot();
  EXPECT_EQ(snapshot.count, 3U);
  EXPECT_EQ(snapshot.sum, 120U);
  EXPECT_EQ(snapshot.min, 10U);
  EXPECT_EQ(snapshot.max, 70U);
  EXPECT_EQ(snapshot.out_of_range_count, 0U);
}

TEST(HistogramTest, RecordsZeroAndTheTopOfItsRange) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_edges_us"));

  ASSERT_TRUE(histogram->record(0).has_value());
  ASSERT_TRUE(histogram->record(kSweepRange).has_value());

  const HistogramSnapshot snapshot = histogram->snapshot();
  EXPECT_EQ(snapshot.count, 2U);
  EXPECT_EQ(snapshot.min, 0U);
  EXPECT_EQ(snapshot.max, kSweepRange);
  EXPECT_LE(relative_error(histogram->value_at_quantile(0.999), kSweepRange), kLayoutRelativeError);
}

TEST(HistogramTest, RejectsAValueAboveItsRangeAndCountsIt) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_overflow_us"));
  ASSERT_TRUE(histogram->record(5).has_value());

  const core::expected<void> rejected = histogram->record(kSweepRange + 1);

  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), core::ErrorCode::kInternalOutOfRange);
  const HistogramSnapshot snapshot = histogram->snapshot();
  EXPECT_EQ(snapshot.out_of_range_count, 1U);
  EXPECT_EQ(snapshot.count, 1U) << "a rejected value must not join the distribution";
  EXPECT_EQ(snapshot.max, 5U);
}

TEST(HistogramTest, StaysWithinItsLayoutErrorAcrossTheWholeRange) {
  // Every value in the range exactly once, so the value at rank k is k-1 and
  // the check covers every magnitude boundary rather than a chosen few.
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_sweep_us"));
  for (std::uint64_t value = 0; value <= kSweepRange; ++value) {
    ASSERT_TRUE(histogram->record(value).has_value()) << "at value " << value;
  }

  const std::size_t total = static_cast<std::size_t>(kSweepRange) + 1;
  ASSERT_EQ(histogram->count(), total);

  double worst_error = 0.0;
  std::uint64_t worst_at = 0;
  for (std::size_t rank = 1; rank <= total; ++rank) {
    // The phi that lands exactly on this rank under the histogram's rounding.
    const double phi = (static_cast<double>(rank) - 0.25) / static_cast<double>(total);
    const std::uint64_t expected = static_cast<std::uint64_t>(rank) - 1;
    const std::uint64_t measured = histogram->value_at_quantile(phi);
    const double error = relative_error(measured, expected);
    if (error > worst_error) {
      worst_error = error;
      worst_at = expected;
    }
    ASSERT_GE(measured, expected) << "the reported value is never below the true one, at rank "
                                  << rank;
  }

  EXPECT_LE(worst_error, kLayoutRelativeError)
      << "worst at value " << worst_at << ", which is where the bucket arithmetic is wrong";
}

TEST(HistogramTest, MatchesAnExactPercentileOnAMillionSamples) {
  const auto histogram = std::make_unique<ReferenceHistogram>(spec_of("volt_test_reference_us"));
  std::vector<std::uint64_t> samples;
  samples.reserve(kReferenceSampleCount);

  std::mt19937_64 random{kSampleSeed};
  std::uniform_real_distribution<double> chooser{0.0, 1.0};
  std::uniform_int_distribution<std::uint64_t> fast{1, kFastSampleCeiling};
  std::uniform_int_distribution<std::uint64_t> slow{kFastSampleCeiling, kSlowSampleCeiling};
  for (std::size_t index = 0; index < kReferenceSampleCount; ++index) {
    const std::uint64_t sample = chooser(random) < kSlowSampleShare ? slow(random) : fast(random);
    samples.push_back(sample);
    ASSERT_TRUE(histogram->record(sample).has_value()) << "seed=" << kSampleSeed;
  }
  std::ranges::sort(samples);

  const HistogramSnapshot snapshot = histogram->snapshot();
  ASSERT_EQ(snapshot.count, kReferenceSampleCount);
  ASSERT_EQ(snapshot.min, samples.front());
  ASSERT_EQ(snapshot.max, samples.back());

  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    const std::uint64_t expected = exact_quantile(samples, quantile.phi);
    const double error = relative_error(quantile.value, expected);
    EXPECT_LE(error, kPermittedRelativeError)
        << "seed=" << kSampleSeed << " quantile=" << quantile.phi << " reported=" << quantile.value
        << " exact=" << expected;
    EXPECT_LE(error, kLayoutRelativeError)
        << "seed=" << kSampleSeed << " the layout promises more than the budget allows";
  }
}

TEST(HistogramTest, SnapshotReportsTheSameQuantilesAsAskingOneByOne) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_agreement_us"));
  std::mt19937_64 random{kSampleSeed};
  std::uniform_int_distribution<std::uint64_t> values{0, kSweepRange};
  for (std::size_t index = 0; index < 50'000; ++index) {
    ASSERT_TRUE(histogram->record(values(random)).has_value()) << "seed=" << kSampleSeed;
  }

  const HistogramSnapshot snapshot = histogram->snapshot();

  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    EXPECT_EQ(quantile.value, histogram->value_at_quantile(quantile.phi))
        << "seed=" << kSampleSeed << " at quantile " << quantile.phi;
  }
}

TEST(HistogramTest, ReportsTheOnlyValueItHoldsAtEveryQuantile) {
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_single_us"));
  ASSERT_TRUE(histogram->record(4096).has_value());

  const HistogramSnapshot snapshot = histogram->snapshot();
  for (const HistogramSnapshot::Quantile &quantile : snapshot.quantiles) {
    EXPECT_LE(relative_error(quantile.value, 4096), kLayoutRelativeError)
        << "at quantile " << quantile.phi;
  }
}

TEST(HistogramTest, SeparatesTheLowTailFromTheHighTail) {
  // Half the observations tiny and half of them large, so a quantile function
  // that ignored its argument could not pass.
  const auto histogram = std::make_unique<SweepHistogram>(spec_of("volt_test_bimodal_us"));
  for (std::size_t index = 0; index < 1000; ++index) {
    ASSERT_TRUE(histogram->record(10).has_value());
    ASSERT_TRUE(histogram->record(90'000).has_value());
  }

  EXPECT_LE(relative_error(histogram->value_at_quantile(0.25), 10), kLayoutRelativeError);
  EXPECT_LE(relative_error(histogram->value_at_quantile(0.75), 90'000), kLayoutRelativeError);
}

} // namespace
} // namespace volt::metrics
