#include "volt/metrics/counter.hpp"
#include "volt/metrics/exposition_writer.hpp"
#include "volt/metrics/gauge.hpp"
#include "volt/metrics/histogram.hpp"
#include "volt/metrics/label.hpp"
#include "volt/metrics/metric_registry.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/prometheus_exporter.hpp"
#include "volt/metrics/text_exporter.hpp"

#include "volt/core/types.hpp"
#include "volt/memory/allocation_tracker.hpp"
#include "volt/memory/no_alloc_scope.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/thread.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace volt::metrics {
namespace {

// Whether a sanitizer runtime, rather than platform/memory, defines the
// allocation operators in this build. Taken from the build rather than from
// the counters, so that a tracker which stopped counting fails the suite that
// depends on it instead of quietly skipping.
[[nodiscard]] constexpr bool sanitizer_owns_heap() noexcept {
#if defined(VOLT_SANITIZER_OWNS_HEAP)
  return true;
#else
  return false;
#endif
}

// A range and precision small enough to keep a histogram on the stack of a
// test, and still wide enough to cross several magnitude boundaries.
constexpr std::uint64_t kTestRange = 100'000;
using TestHistogram = Histogram<kTestRange, 3>;

// Room for the expositions in this file, which run to a few hundred bytes.
constexpr std::size_t kExpositionBytes = 4096;

// These helper threads are normal-priority and share the runner's CPU set, so
// zero priority and zero mask are the portable PAL values for that policy.
constexpr pal::SchedulingPolicy kTestPolicy = pal::SchedulingPolicy::kOther;
constexpr core::Priority kTestPriority{};
constexpr pal::CpuMask kInheritedCpuMask = 0;
constexpr std::size_t kDefaultStackBytes = 0;

[[nodiscard]] constexpr pal::ThreadConfig thread_config(std::string_view name) noexcept {
  return pal::ThreadConfig{.name = name,
                           .policy = kTestPolicy,
                           .priority = kTestPriority,
                           .cpu_mask = kInheritedCpuMask,
                           .stack_bytes = kDefaultStackBytes};
}

[[nodiscard]] MetricSpec spec_of(std::string_view name, std::span<const Label> labels = {}) {
  return MetricSpec{.name = name, .help = "A metric under test.", .labels = labels};
}

TEST(CounterTest, StartsAtZeroAndAddsEveryIncrement) {
  Counter counter{spec_of("volt_test_events_total")};
  EXPECT_EQ(counter.value(), 0U);

  counter.increment();
  counter.increment();
  counter.add(40);

  EXPECT_EQ(counter.value(), 42U);
}

TEST(CounterTest, CountsEveryIncrementWhenManyThreadsAddAtOnce) {
  // Four threads and a quarter of a million each: enough contention that a
  // non-atomic increment loses updates in practice, not just in principle.
  constexpr std::size_t kThreadCount = 4;
  constexpr std::uint64_t kIncrementsPerThread = 250'000;

  Counter counter{spec_of("volt_test_contended_total")};
  pal::posix::PosixPlatform platform;
  std::array<std::unique_ptr<pal::IThread>, kThreadCount> threads{};

  for (std::size_t index = 0; index < kThreadCount; ++index) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        platform.create_thread(thread_config("volt-counter"), [&counter] {
          for (std::uint64_t step = 0; step < kIncrementsPerThread; ++step) {
            counter.increment();
          }
        });
    ASSERT_TRUE(thread.has_value());
    threads[index] = std::move(*thread);
  }
  for (const std::unique_ptr<pal::IThread> &thread : threads) {
    ASSERT_TRUE(thread->join().has_value());
  }

  EXPECT_EQ(counter.value(), kThreadCount * kIncrementsPerThread);
}

TEST(GaugeTest, ReportsTheValueLastWritten) {
  Gauge gauge{spec_of("volt_test_load_ratio")};
  EXPECT_EQ(gauge.value(), 0.0);

  gauge.set(0.25);
  EXPECT_EQ(gauge.value(), 0.25);

  gauge.set(-3.5);
  EXPECT_EQ(gauge.value(), -3.5);
}

TEST(MetricRegistryTest, CollectsEveryMetricInRegistrationOrder) {
  MetricRegistry registry;
  Counter first{spec_of("volt_test_first_total")};
  Gauge second{spec_of("volt_test_second_ratio")};
  first.add(7);
  second.set(1.5);

  ASSERT_TRUE(registry.add(first).has_value());
  ASSERT_TRUE(registry.add(second).has_value());
  EXPECT_EQ(registry.size(), 2U);

  std::array<char, kExpositionBytes> buffer{};
  TextExporter exporter{buffer};
  registry.collect(exporter);

  const std::string_view dump = exporter.view();
  const std::size_t first_at = dump.find("volt_test_first_total");
  const std::size_t second_at = dump.find("volt_test_second_ratio");
  ASSERT_NE(first_at, std::string_view::npos);
  ASSERT_NE(second_at, std::string_view::npos);
  EXPECT_LT(first_at, second_at);
}

TEST(MetricRegistryTest, RefusesASecondMetricWithTheSameNameAndLabels) {
  constexpr std::array<Label, 1> labels{Label{.name = "node", .value = "a"}};
  MetricRegistry registry;
  Counter original{spec_of("volt_test_duplicate_total", labels)};
  Counter twin{spec_of("volt_test_duplicate_total", labels)};

  ASSERT_TRUE(registry.add(original).has_value());
  const core::expected<void> rejected = registry.add(twin);

  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), core::ErrorCode::kConfigDuplicateId);
  EXPECT_EQ(registry.size(), 1U);
}

TEST(MetricRegistryTest, AcceptsOneNameCarryingDifferentLabelSets) {
  constexpr std::array<Label, 1> front{Label{.name = "wheel", .value = "front"}};
  constexpr std::array<Label, 1> rear{Label{.name = "wheel", .value = "rear"}};
  MetricRegistry registry;
  Counter front_counter{spec_of("volt_test_wheel_total", front)};
  Counter rear_counter{spec_of("volt_test_wheel_total", rear)};

  ASSERT_TRUE(registry.add(front_counter).has_value());
  ASSERT_TRUE(registry.add(rear_counter).has_value());

  EXPECT_EQ(registry.size(), 2U);
  EXPECT_EQ(registry.find(front_counter.spec()), &front_counter);
  EXPECT_EQ(registry.find(rear_counter.spec()), &rear_counter);
}

TEST(MetricRegistryTest, ReportsExhaustionRatherThanGrowing) {
  MetricRegistry registry;
  std::vector<std::unique_ptr<Counter>> counters;
  std::vector<std::string> names;
  counters.reserve(kMaxMetrics + 1);
  names.reserve(kMaxMetrics + 1);

  for (std::size_t index = 0; index <= kMaxMetrics; ++index) {
    names.push_back("volt_test_bulk_" + std::to_string(index) + "_total");
  }
  for (std::size_t index = 0; index <= kMaxMetrics; ++index) {
    counters.push_back(std::make_unique<Counter>(spec_of(names[index])));
  }

  for (std::size_t index = 0; index < kMaxMetrics; ++index) {
    ASSERT_TRUE(registry.add(*counters[index]).has_value()) << "at index " << index;
  }
  const core::expected<void> overflow = registry.add(*counters[kMaxMetrics]);

  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(registry.size(), kMaxMetrics);
}

TEST(MetricRegistryTest, FindsNothingForAnUnregisteredSeries) {
  MetricRegistry registry;
  Counter registered{spec_of("volt_test_present_total")};
  ASSERT_TRUE(registry.add(registered).has_value());

  const MetricSpec absent = spec_of("volt_test_absent_total");
  EXPECT_EQ(registry.find(absent), nullptr);
}

TEST(ExpositionWriterTest, StopsAtTheEndOfTheBufferAndSaysSo) {
  std::array<char, 8> buffer{};
  ExpositionWriter writer{buffer};

  writer.append("12345");
  EXPECT_FALSE(writer.truncated());
  writer.append("6789");

  EXPECT_TRUE(writer.truncated());
  EXPECT_EQ(writer.view(), "12345");
}

TEST(ExpositionWriterTest, ReportsOverflowWhileEscapingCharacterByCharacter) {
  // The escaping paths append one character at a time rather than in one
  // block, so they run out of room in a different place from a plain append.
  std::array<char, 4> buffer{};
  ExpositionWriter writer{buffer};

  writer.append_label_value("abcdefgh");

  EXPECT_TRUE(writer.truncated());
  EXPECT_EQ(writer.view(), "abcd");
}

TEST(ExpositionWriterTest, ReportsOverflowWhenAnEscapeIsWhatDoesNotFit) {
  std::array<char, 3> buffer{};
  ExpositionWriter writer{buffer};

  // Two characters, then a quote that becomes two more and cannot fit.
  writer.append_label_value("ab\"");

  EXPECT_TRUE(writer.truncated());
}

TEST(ExpositionWriterTest, EscapesWhatEachContextRequires) {
  std::array<char, 64> help_buffer{};
  ExpositionWriter help{help_buffer};
  help.append_help("a\\b\nc\"d");
  EXPECT_EQ(help.view(), "a\\\\b\\nc\"d");

  std::array<char, 64> label_buffer{};
  ExpositionWriter label{label_buffer};
  label.append_label_value("a\\b\nc\"d");
  EXPECT_EQ(label.view(), "a\\\\b\\nc\\\"d");
}

TEST(ExpositionWriterTest, SpellsTheDoublesThatHaveNoDecimalForm) {
  std::array<char, 64> buffer{};
  ExpositionWriter writer{buffer};

  writer.append(std::numeric_limits<double>::infinity());
  writer.append(" ");
  writer.append(-std::numeric_limits<double>::infinity());
  writer.append(" ");
  writer.append(std::numeric_limits<double>::quiet_NaN());
  writer.append(" ");
  writer.append(0.5);

  EXPECT_EQ(writer.view(), "+Inf -Inf NaN 0.5");
}

TEST(PrometheusExporterTest, WritesOneHelpAndTypeForAFamilyWithSeveralLabelSets) {
  constexpr std::array<Label, 1> front{Label{.name = "wheel", .value = "front"}};
  constexpr std::array<Label, 1> rear{Label{.name = "wheel", .value = "rear"}};
  MetricRegistry registry;
  Counter front_counter{spec_of("volt_test_wheel_total", front)};
  Counter rear_counter{spec_of("volt_test_wheel_total", rear)};
  front_counter.add(3);
  rear_counter.add(4);
  ASSERT_TRUE(registry.add(front_counter).has_value());
  ASSERT_TRUE(registry.add(rear_counter).has_value());

  std::array<char, kExpositionBytes> buffer{};
  PrometheusExporter exporter{buffer};
  registry.collect(exporter);

  EXPECT_EQ(exporter.view(), "# HELP volt_test_wheel_total A metric under test.\n"
                             "# TYPE volt_test_wheel_total counter\n"
                             "volt_test_wheel_total{wheel=\"front\"} 3\n"
                             "volt_test_wheel_total{wheel=\"rear\"} 4\n");
  EXPECT_FALSE(exporter.truncated());
  EXPECT_FALSE(exporter.families_overflowed());
}

TEST(PrometheusExporterTest, WritesAGaugeWithNoLabelsAsABareSeries) {
  MetricRegistry registry;
  Gauge gauge{spec_of("volt_test_temperature_celsius")};
  gauge.set(41.5);
  ASSERT_TRUE(registry.add(gauge).has_value());

  std::array<char, kExpositionBytes> buffer{};
  PrometheusExporter exporter{buffer};
  registry.collect(exporter);

  EXPECT_EQ(exporter.view(), "# HELP volt_test_temperature_celsius A metric under test.\n"
                             "# TYPE volt_test_temperature_celsius gauge\n"
                             "volt_test_temperature_celsius 41.5\n");
}

TEST(PrometheusExporterTest, WritesAHistogramAsASummaryWithItsSumCountAndOverflow) {
  MetricRegistry registry;
  TestHistogram histogram{spec_of("volt_test_latency_us")};
  ASSERT_TRUE(histogram.record(10).has_value());
  ASSERT_TRUE(histogram.record(20).has_value());
  ASSERT_FALSE(histogram.record(kTestRange + 1).has_value());
  ASSERT_TRUE(registry.add(histogram).has_value());

  std::array<char, kExpositionBytes> buffer{};
  PrometheusExporter exporter{buffer};
  registry.collect(exporter);

  const std::string_view exposition = exporter.view();
  EXPECT_NE(exposition.find("# TYPE volt_test_latency_us summary\n"), std::string_view::npos);
  EXPECT_NE(exposition.find("# TYPE volt_test_latency_us_out_of_range_total counter\n"),
            std::string_view::npos);
  EXPECT_NE(exposition.find("volt_test_latency_us{quantile=\"0.5\"} 10\n"), std::string_view::npos);
  EXPECT_NE(exposition.find("volt_test_latency_us{quantile=\"0.999\"} 20\n"),
            std::string_view::npos);
  EXPECT_NE(exposition.find("volt_test_latency_us_sum 30\n"), std::string_view::npos);
  EXPECT_NE(exposition.find("volt_test_latency_us_count 2\n"), std::string_view::npos);
  EXPECT_NE(exposition.find("volt_test_latency_us_out_of_range_total 1\n"), std::string_view::npos);
}

TEST(PrometheusExporterTest, EscapesALabelValueThatWouldCloseItsQuotes) {
  constexpr std::array<Label, 1> labels{Label{.name = "path", .value = "a\"b\\c"}};
  MetricRegistry registry;
  Counter counter{spec_of("volt_test_escaped_total", labels)};
  ASSERT_TRUE(registry.add(counter).has_value());

  std::array<char, kExpositionBytes> buffer{};
  PrometheusExporter exporter{buffer};
  registry.collect(exporter);

  EXPECT_NE(exporter.view().find("volt_test_escaped_total{path=\"a\\\"b\\\\c\"} 0\n"),
            std::string_view::npos);
}

TEST(PrometheusExporterTest, ReportsTruncationRatherThanOverrunningItsBuffer) {
  MetricRegistry registry;
  Counter counter{spec_of("volt_test_long_total")};
  ASSERT_TRUE(registry.add(counter).has_value());

  std::array<char, 16> buffer{};
  PrometheusExporter exporter{buffer};
  registry.collect(exporter);

  EXPECT_TRUE(exporter.truncated());
  EXPECT_LE(exporter.written(), buffer.size());
}

TEST(TextExporterTest, WritesOneReadableLinePerMetric) {
  constexpr std::array<Label, 1> labels{Label{.name = "node", .value = "a"}};
  MetricRegistry registry;
  Counter counter{spec_of("volt_test_lines_total", labels)};
  TestHistogram histogram{spec_of("volt_test_spread_us")};
  counter.add(5);
  ASSERT_TRUE(histogram.record(7).has_value());
  ASSERT_TRUE(registry.add(counter).has_value());
  ASSERT_TRUE(registry.add(histogram).has_value());

  std::array<char, kExpositionBytes> buffer{};
  TextExporter exporter{buffer};
  registry.collect(exporter);

  const std::string_view dump = exporter.view();
  EXPECT_EQ(std::ranges::count(dump, '\n'), 2);
  EXPECT_NE(dump.find("volt_test_lines_total node=a"), std::string_view::npos);
  EXPECT_NE(dump.find("count=1 min=7 max=7 sum=7"), std::string_view::npos);
  EXPECT_FALSE(exporter.truncated());
}

TEST(MetricsAllocationTest, RecordingFromTheDataPlaneAllocatesNothing) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  // Built before the guard, because building a metric is startup work; what
  // has to be allocation-free is recording into one.
  Counter counter{spec_of("volt_test_guarded_total")};
  Gauge gauge{spec_of("volt_test_guarded_ratio")};
  auto histogram = std::make_unique<TestHistogram>(spec_of("volt_test_guarded_us"));
  MetricRegistry registry;
  ASSERT_TRUE(registry.add(counter).has_value());
  ASSERT_TRUE(registry.add(gauge).has_value());
  ASSERT_TRUE(registry.add(*histogram).has_value());

  const memory::AllocationStats before = memory::AllocationTracker::current_thread_stats();
  std::uint64_t violations = 0;
  bool recorded = true;
  {
    const memory::no_alloc_scope guard;
    for (std::uint64_t step = 0; step < 1000; ++step) {
      counter.increment();
      gauge.set(static_cast<double>(step));
      recorded = recorded && histogram->record(step).has_value();
    }
    violations = guard.violations();
  }
  const memory::AllocationStats after = memory::AllocationTracker::current_thread_stats();

  EXPECT_TRUE(recorded);
  EXPECT_EQ(violations, 0U);
  EXPECT_EQ(after.allocation_count, before.allocation_count);
  EXPECT_EQ(counter.value(), 1000U);
  EXPECT_EQ(histogram->count(), 1000U);
}

TEST(MetricsAllocationTest, ScrapingAnEntireRegistryAllocatesNothing) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  MetricRegistry registry;
  Counter counter{spec_of("volt_test_scrape_total")};
  Gauge gauge{spec_of("volt_test_scrape_ratio")};
  auto histogram = std::make_unique<TestHistogram>(spec_of("volt_test_scrape_us"));
  ASSERT_TRUE(histogram->record(11).has_value());
  ASSERT_TRUE(registry.add(counter).has_value());
  ASSERT_TRUE(registry.add(gauge).has_value());
  ASSERT_TRUE(registry.add(*histogram).has_value());
  auto buffer = std::make_unique<std::array<char, kExpositionBytes>>();

  const memory::AllocationStats before = memory::AllocationTracker::current_thread_stats();
  std::size_t written = 0;
  {
    const memory::no_alloc_scope guard;
    PrometheusExporter exporter{*buffer};
    registry.collect(exporter);
    written = exporter.written();
  }
  const memory::AllocationStats after = memory::AllocationTracker::current_thread_stats();

  EXPECT_GT(written, 0U);
  EXPECT_EQ(after.allocation_count, before.allocation_count);
}

} // namespace
} // namespace volt::metrics
