#include "volt/log/log_drain.hpp"
#include "volt/log/logger.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace volt::log {
namespace {

// Set by the build when any sanitizer is on; see cmake/Sanitizers.cmake.
#if defined(VOLT_SANITIZER_ACTIVE)
constexpr bool kUnderSanitizer = true;
#else
constexpr bool kUnderSanitizer = false;
#endif

// Calls per timed batch. One clock read costs tens of nanoseconds on its own,
// which is the same order as the thing being measured, so the reads bracket a
// batch instead of each call and the cost is divided out.
constexpr int kCallsPerBatch = 1000;

// Batches kept. Enough that a percentile means something without the run
// taking long enough to matter in the suite.
constexpr int kBatches = 2000;

// The budget of SPEC 8.4: a log call on the control path costs tens of
// nanoseconds, never a microsecond. Asserted on the median, because a
// percentile at the tail is at the mercy of whatever else the machine is
// doing while the suite runs, and a shared runner is not a quiet machine.
constexpr std::int64_t kCallerBudgetNs = 100;

/// Discards records as fast as it is handed them, so the measurement is of the
/// producer path and not of a disk.
class NullSink final : public ILogSink {
public:
  [[nodiscard]] core::expected<void> write(std::span<const std::byte> record) noexcept override {
    static_cast<void>(record);
    return {};
  }
  [[nodiscard]] core::expected<void> flush() noexcept override { return {}; }
};

[[nodiscard]] std::int64_t percentile(const std::vector<std::int64_t> &sorted,
                                      std::size_t per_thousand) {
  constexpr std::size_t kThousand = 1000;
  const std::size_t index = (sorted.size() * per_thousand) / kThousand;
  return sorted[std::min(index, sorted.size() - 1)];
}

TEST(LogBenchmarkTest, CallerPathStaysWithinItsBudget) {
  if (kUnderSanitizer) {
    // A sanitizer instruments every memory access, so a nanosecond figure
    // taken under one measures the tooling rather than the code. The skip is
    // decided at run time so the body still compiles under every
    // configuration; only the assertion on time is meaningless there.
    GTEST_SKIP() << "timing is not measurable under a sanitizer";
  }

  pal::posix::PosixPlatform platform;
  Logger::instance().set_clock(platform.clock());
  Logger::instance().set_level_for_all(Level::kTrace);
  Logger::prepare_current_thread();

  NullSink sink;
  LogDrain drain{Logger::instance(), sink, platform};
  ASSERT_TRUE(drain.start().has_value());

  // Warm up so the measurement excludes the first-touch page faults on the
  // ring and the branch predictor learning the path.
  for (int index = 0; index < kCallsPerBatch; ++index) {
    VOLT_LOG_INFO(Module::kCore, "warmup {}", index);
  }

  std::vector<std::int64_t> per_call_ns;
  per_call_ns.reserve(kBatches);
  for (int batch = 0; batch < kBatches; ++batch) {
    const core::Timestamp start = platform.clock().monotonic();
    for (int index = 0; index < kCallsPerBatch; ++index) {
      VOLT_LOG_INFO(Module::kCore, "benchmark {} batch {}", index, batch);
    }
    const core::Timestamp finish = platform.clock().monotonic();
    per_call_ns.push_back(finish.checked_since(start).value().ns() / kCallsPerBatch);
  }
  drain.stop();

  std::ranges::sort(per_call_ns);
  const std::int64_t median = percentile(per_call_ns, 500);
  const std::int64_t tail = percentile(per_call_ns, 990);

  RecordProperty("caller_ns_p50", static_cast<int>(median));
  RecordProperty("caller_ns_p99", static_cast<int>(tail));

  EXPECT_LT(median, kCallerBudgetNs) << "p50=" << median << "ns p99=" << tail << "ns over "
                                     << kBatches << " batches of " << kCallsPerBatch;
}

} // namespace
} // namespace volt::log
