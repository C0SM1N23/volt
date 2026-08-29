#include "volt/metrics/counter.hpp"
#include "volt/metrics/gauge.hpp"
#include "volt/metrics/histogram.hpp"
#include "volt/metrics/label.hpp"
#include "volt/metrics/metric_registry.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/prometheus_exporter.hpp"

#include "volt/pal/file.hpp"
#include "volt/pal/platform.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/process.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace volt::metrics {
namespace {

// Where python3 lives on the images VOLT builds on. A machine without it skips
// the reference check rather than failing it, the way the trace suite does.
constexpr std::string_view kPythonPath = "/usr/bin/python3";

constexpr std::uint64_t kHistogramRange = 100'000;
using TestHistogram = Histogram<kHistogramRange, 3>;

constexpr std::size_t kExpositionBytes = 8192;

/// Writes `text` to `path`, reporting whether it landed.
[[nodiscard]] bool write_text(pal::IPlatform &platform, std::string_view path,
                              std::string_view text) {
  core::expected<std::unique_ptr<pal::IFile>> file =
      platform.open_file(path, pal::FileMode::kWrite);
  if (!file.has_value()) {
    return false;
  }
  const std::span<const std::byte> bytes{reinterpret_cast<const std::byte *>(text.data()),
                                         text.size()};
  return (*file)->write(bytes).has_value() && (*file)->flush().has_value();
}

/// Runs the reference parser over `path` and returns its exit code.
///
/// @post returns nothing when python3 could not be started at all
[[nodiscard]] core::expected<std::int32_t> validate(pal::IPlatform &platform,
                                                    std::string_view path) {
  const std::array<std::string_view, 2> arguments{VOLT_METRICS_VALIDATOR, path};
  core::expected<std::unique_ptr<pal::IProcess>> validator =
      platform.spawn_process(pal::ProcessConfig{.executable = kPythonPath, .arguments = arguments});
  if (!validator.has_value()) {
    return std::unexpected{validator.error()};
  }

  const core::expected<pal::ProcessExit> exit = (*validator)->wait();
  if (!exit.has_value() || exit->reason != pal::ExitReason::kReturned) {
    return std::unexpected{core::ErrorCode::kExternalRequestRejected};
  }
  return exit->code;
}

/// Fills `buffer` with an exposition holding one of every metric shape.
[[nodiscard]] std::string_view render_sample(std::span<char> buffer, MetricRegistry &registry,
                                             Counter &counter, Gauge &gauge,
                                             TestHistogram &histogram) {
  counter.add(9);
  gauge.set(0.5);
  VOLT_ASSERT(histogram.record(15).has_value(), "a value inside the range was refused");
  VOLT_ASSERT(histogram.record(2500).has_value(), "a value inside the range was refused");
  VOLT_ASSERT(!histogram.record(kHistogramRange + 1).has_value(),
              "a value above the range was accepted");
  VOLT_ASSERT(registry.add(counter).has_value(), "the counter did not register");
  VOLT_ASSERT(registry.add(gauge).has_value(), "the gauge did not register");
  VOLT_ASSERT(registry.add(histogram).has_value(), "the histogram did not register");

  PrometheusExporter exporter{buffer};
  registry.collect(exporter);
  VOLT_ASSERT(!exporter.truncated(), "the sample exposition did not fit its buffer");
  return exporter.view();
}

TEST(ExpositionValidationTest, ProducesAnExpositionTheReferenceParserAccepts) {
  // Checked by a script that shares no code with the exporter, so a mistake in
  // how VOLT writes the format cannot hide behind the same mistake in how it
  // reads it back.
  static constexpr std::array<Label, 2> kLabels{Label{.name = "node", .value = "a"},
                                                Label{.name = "path", .value = "we\"ird\\one"}};
  pal::posix::PosixPlatform platform;
  MetricRegistry registry;
  Counter counter{MetricSpec{.name = "volt_test_requests_total",
                             .help = "Requests seen, with a \\ and a\nnewline in the help.",
                             .labels = kLabels}};
  Gauge gauge{MetricSpec{
      .name = "volt_test_load_ratio", .help = "Share of capacity in use.", .labels = kLabels}};
  TestHistogram histogram{MetricSpec{
      .name = "volt_test_service_us", .help = "Service time in microseconds.", .labels = kLabels}};

  std::array<char, kExpositionBytes> buffer{};
  const std::string_view exposition = render_sample(buffer, registry, counter, gauge, histogram);
  ASSERT_FALSE(exposition.empty());
  ASSERT_TRUE(write_text(platform, VOLT_METRICS_SAMPLE_PATH, exposition));

  const core::expected<std::int32_t> code = validate(platform, VOLT_METRICS_SAMPLE_PATH);
  if (!code.has_value()) {
    GTEST_SKIP() << "no python3 to run the reference validator with";
  }
  EXPECT_EQ(*code, 0) << "the reference parser rejected " << VOLT_METRICS_SAMPLE_PATH;
}

TEST(ExpositionValidationTest, TheReferenceParserRejectsAnExpositionThatIsWrong) {
  // A validator that accepted anything would make the test above meaningless,
  // so it is shown here refusing a file that breaks one rule of the format:
  // a sample whose family was never declared.
  pal::posix::PosixPlatform platform;
  ASSERT_TRUE(write_text(platform, VOLT_METRICS_BROKEN_PATH,
                         "# HELP volt_test_undeclared_total Help without a type.\n"
                         "volt_test_undeclared_total 1\n"));

  const core::expected<std::int32_t> code = validate(platform, VOLT_METRICS_BROKEN_PATH);
  if (!code.has_value()) {
    GTEST_SKIP() << "no python3 to run the reference validator with";
  }
  EXPECT_NE(*code, 0) << "the reference parser accepted an exposition it should refuse";
}

} // namespace
} // namespace volt::metrics
