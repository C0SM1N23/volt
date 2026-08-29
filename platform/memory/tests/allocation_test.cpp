#include "volt/memory/allocation_metrics.hpp"
#include "volt/memory/allocation_stats.hpp"
#include "volt/memory/allocation_tracker.hpp"
#include "volt/memory/no_alloc_scope.hpp"

#include "volt/core/types.hpp"
#include "volt/metrics/histogram_snapshot.hpp"
#include "volt/metrics/metric_registry.hpp"
#include "volt/metrics/metric_visitor.hpp"
#include "volt/metrics/text_exporter.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/thread.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace volt::memory {
namespace {

// Every test below that counts allocations first asks whether VOLT's allocator
// is the one running. Under a sanitizer it is not, and the counters would read
// zero for a reason that has nothing to do with the code under test: a pass
// there would mean nothing at all.
//
// The answer comes from the build rather than from the counters, deliberately.
// Asking the counters would turn "the tracker stopped counting" into a skipped
// test instead of a failing one.
[[nodiscard]] constexpr bool sanitizer_owns_heap() noexcept {
#if defined(VOLT_SANITIZER_OWNS_HEAP)
  return true;
#else
  return false;
#endif
}

// A size no other allocation in the test is likely to use, so that a counter
// that moved by exactly this much moved because of this test's block.
constexpr std::size_t kBlockBytes = 4093;
// Small enough that allocating it after the block above cannot raise the
// high-water mark, which is what makes a peak that tracks the live value fail.
constexpr std::size_t kSmallBlockBytes = 17;

// A vector of bytes asks its allocator for exactly one byte per element, so
// the byte counters can be predicted rather than approximated.
using ByteBlock = std::vector<std::byte>;

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

TEST(AllocationTrackerTest, KnowsWhetherItsOwnOperatorsAreTheOnesInUse) {
  EXPECT_EQ(AllocationTracker::hooks_installed(), !sanitizer_owns_heap());
}

TEST(AllocationTrackerTest, CountsOneAllocationAndItsRelease) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  const AllocationStats before = AllocationTracker::current_thread_stats();

  AllocationStats during{};
  {
    const ByteBlock block(kBlockBytes);
    during = AllocationTracker::current_thread_stats();
  }
  const AllocationStats after = AllocationTracker::current_thread_stats();

  EXPECT_EQ(during.allocation_count, before.allocation_count + 1);
  EXPECT_EQ(during.total_bytes, before.total_bytes + kBlockBytes);
  EXPECT_EQ(during.live_bytes, before.live_bytes + kBlockBytes);
  EXPECT_EQ(during.live_allocations, before.live_allocations + 1);

  EXPECT_EQ(after.deallocation_count, before.deallocation_count + 1);
  EXPECT_EQ(after.live_bytes, before.live_bytes);
  EXPECT_EQ(after.live_allocations, before.live_allocations);
  EXPECT_EQ(after.total_bytes, before.total_bytes + kBlockBytes);
}

TEST(AllocationTrackerTest, KeepsTheHighWaterMarkAfterTheBlockIsReleased) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  AllocationStats during{};
  {
    const ByteBlock block(kBlockBytes);
    during = AllocationTracker::current_thread_stats();
  }
  const AllocationStats after = AllocationTracker::current_thread_stats();

  EXPECT_GE(during.peak_live_bytes, during.live_bytes);
  EXPECT_EQ(after.peak_live_bytes, during.peak_live_bytes);
  EXPECT_EQ(after.peak_live_allocations, during.peak_live_allocations);
  EXPECT_GT(after.peak_live_bytes, after.live_bytes);
}

TEST(AllocationTrackerTest, KeepsTheHighWaterMarkWhenALaterBlockIsSmaller) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  AllocationStats at_high_water{};
  {
    const ByteBlock large(kBlockBytes);
    at_high_water = AllocationTracker::current_thread_stats();
  }

  AllocationStats at_low_water{};
  {
    const ByteBlock small(kSmallBlockBytes);
    at_low_water = AllocationTracker::current_thread_stats();
  }

  EXPECT_LT(at_low_water.live_bytes, at_high_water.live_bytes);
  EXPECT_EQ(at_low_water.peak_live_bytes, at_high_water.peak_live_bytes);
}

TEST(AllocationTrackerTest, CountsEveryThreadThatAllocatesIntoTheProcessTotal) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  const AllocationStats before = AllocationTracker::instance().process_stats();
  const std::size_t threads_before = AllocationTracker::instance().tracked_threads();

  pal::posix::PosixPlatform platform;
  const core::expected<std::unique_ptr<pal::IThread>> worker =
      platform.create_thread(thread_config("volt-alloc-count"), [] {
        const AllocationStats start = AllocationTracker::current_thread_stats();
        const ByteBlock block(kBlockBytes);
        const AllocationStats held = AllocationTracker::current_thread_stats();
        EXPECT_EQ(held.live_bytes, start.live_bytes + kBlockBytes);
      });
  ASSERT_TRUE(worker.has_value());
  ASSERT_TRUE((*worker)->join().has_value());

  const AllocationStats after = AllocationTracker::instance().process_stats();
  EXPECT_EQ(AllocationTracker::instance().tracked_threads(), threads_before + 1);
  EXPECT_GE(after.total_bytes, before.total_bytes + kBlockBytes);
  EXPECT_EQ(AllocationTracker::instance().threads_refused(), 0U);
}

TEST(AllocationTrackerTest, CreditsAReleaseToTheThreadThatAllocated) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  const AllocationStats before = AllocationTracker::instance().process_stats();

  ByteBlock handover;
  {
    pal::posix::PosixPlatform platform;
    const core::expected<std::unique_ptr<pal::IThread>> producer = platform.create_thread(
        thread_config("volt-alloc-owner"), [&handover] { handover = ByteBlock(kBlockBytes); });
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE((*producer)->join().has_value());
  }

  // Releasing here what another thread allocated is the case that drives a
  // per-thread byte counter below zero if the block is credited back to
  // whichever thread happens to free it.
  handover = ByteBlock{};

  // Stated as a bound rather than an equality because the process total also
  // carries whatever the test framework holds at each snapshot. A counter that
  // went below zero wraps to the top of its range, which no bound survives.
  const AllocationStats after = AllocationTracker::instance().process_stats();
  EXPECT_LE(after.live_bytes, before.live_bytes + kBlockBytes);
  EXPECT_LE(after.live_allocations, before.live_allocations + 1);
}

TEST(NoAllocScopeTest, ReportsNothingForCodeThatDoesNotAllocate) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  std::uint64_t violations = 0;
  std::uint64_t sum = 0;
  {
    const no_alloc_scope guard;
    for (std::uint64_t step = 0; step < kBlockBytes; ++step) {
      sum += step;
    }
    violations = guard.violations();
  }

  EXPECT_EQ(violations, 0U);
  EXPECT_GT(sum, 0U);
}

TEST(NoAllocScopeTest, GuardsTheThreadUntilTheOutermostScopeEnds) {
  EXPECT_FALSE(no_alloc_scope::active());
  {
    const no_alloc_scope outer;
    EXPECT_TRUE(no_alloc_scope::active());
    {
      const no_alloc_scope inner;
      EXPECT_TRUE(no_alloc_scope::active());
    }
    EXPECT_TRUE(no_alloc_scope::active());
  }
  EXPECT_FALSE(no_alloc_scope::active());
}

TEST(NoAllocScopeTest, StopsGuardingOnceTheScopeEnds) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  {
    const no_alloc_scope guard;
    EXPECT_TRUE(no_alloc_scope::active());
  }

  const AllocationStats before = AllocationTracker::current_thread_stats();
  const ByteBlock block(kBlockBytes);
  const AllocationStats after = AllocationTracker::current_thread_stats();

  EXPECT_EQ(after.allocation_count, before.allocation_count + 1);
  EXPECT_EQ(after.violation_count, before.violation_count);
}

/// Remembers the value of every metric a collection hands it, by name.
///
/// Allocation-free on purpose: the test collects inside a `no_alloc_scope` so
/// that nothing can allocate between the refresh and the reading it is
/// compared against, which is what makes an exact comparison possible.
class ValueCapture final : public metrics::IMetricVisitor {
public:
  void visit_counter(const metrics::MetricSpec &spec, std::uint64_t value) noexcept override {
    remember(spec.name, static_cast<double>(value));
  }

  void visit_gauge(const metrics::MetricSpec &spec, double value) noexcept override {
    remember(spec.name, value);
  }

  void visit_histogram(const metrics::MetricSpec &spec,
                       const metrics::HistogramSnapshot &snapshot) noexcept override {
    remember(spec.name, static_cast<double>(snapshot.count));
  }

  /// Returns what `name` reported, or nothing when it was never collected.
  [[nodiscard]] core::expected<double> value_of(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
      if (names_[index] == name) {
        return values_[index];
      }
    }
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }

private:
  // Room for the six metrics the bridge publishes, with two to spare.
  static constexpr std::size_t kCapacity = 8;

  void remember(std::string_view name, double value) noexcept {
    if (count_ == kCapacity) {
      return;
    }
    names_[count_] = name;
    values_[count_] = value;
    ++count_;
  }

  std::array<std::string_view, kCapacity> names_{};
  std::array<double, kCapacity> values_{};
  std::size_t count_ = 0;
};

TEST(AllocationMetricsTest, PublishesExactlyWhatTheTrackerCounted) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  metrics::MetricRegistry registry;
  AllocationMetrics published;
  ASSERT_TRUE(published.register_with(registry).has_value());
  EXPECT_TRUE(AllocationMetrics::measurable());

  {
    const ByteBlock block(kBlockBytes);
    EXPECT_EQ(block.size(), kBlockBytes);
  }

  ValueCapture captured;
  AllocationStats observed{};
  {
    // Nothing may allocate between the refresh and the reading it is compared
    // against, or the two would be answers to different questions.
    const no_alloc_scope guard;
    published.refresh();
    registry.collect(captured);
    observed = AllocationTracker::instance().process_stats();
  }

  const core::expected<double> allocations = captured.value_of("volt_alloc_allocations_total");
  const core::expected<double> bytes = captured.value_of("volt_alloc_bytes_total");
  const core::expected<double> live = captured.value_of("volt_alloc_live_bytes");
  const core::expected<double> peak = captured.value_of("volt_alloc_peak_bytes");
  const core::expected<double> violations = captured.value_of("volt_alloc_violations_total");
  const core::expected<double> threads = captured.value_of("volt_alloc_tracked_threads");
  ASSERT_TRUE(allocations.has_value());
  ASSERT_TRUE(bytes.has_value());
  ASSERT_TRUE(live.has_value());
  ASSERT_TRUE(peak.has_value());
  ASSERT_TRUE(violations.has_value());
  ASSERT_TRUE(threads.has_value());

  EXPECT_EQ(*allocations, static_cast<double>(observed.allocation_count));
  EXPECT_EQ(*bytes, static_cast<double>(observed.total_bytes));
  EXPECT_EQ(*live, static_cast<double>(observed.live_bytes));
  EXPECT_EQ(*peak, static_cast<double>(observed.peak_live_bytes));
  EXPECT_EQ(*violations, static_cast<double>(observed.violation_count));
  EXPECT_EQ(*threads, static_cast<double>(AllocationTracker::instance().tracked_threads()));
  EXPECT_GT(*allocations, 0.0) << "the block above has to appear in the total";
}

TEST(AllocationMetricsTest, AdvancesItsCountersByWhatHappenedBetweenRefreshes) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  metrics::MetricRegistry registry;
  AllocationMetrics published;
  ASSERT_TRUE(published.register_with(registry).has_value());

  ValueCapture before;
  {
    const no_alloc_scope guard;
    published.refresh();
    registry.collect(before);
  }

  {
    const ByteBlock block(kBlockBytes);
    EXPECT_EQ(block.size(), kBlockBytes);
  }

  ValueCapture after;
  {
    const no_alloc_scope guard;
    published.refresh();
    registry.collect(after);
  }

  const core::expected<double> allocations_before = before.value_of("volt_alloc_allocations_total");
  const core::expected<double> allocations_after = after.value_of("volt_alloc_allocations_total");
  const core::expected<double> bytes_before = before.value_of("volt_alloc_bytes_total");
  const core::expected<double> bytes_after = after.value_of("volt_alloc_bytes_total");
  ASSERT_TRUE(allocations_before.has_value());
  ASSERT_TRUE(allocations_after.has_value());
  ASSERT_TRUE(bytes_before.has_value());
  ASSERT_TRUE(bytes_after.has_value());

  // One block was allocated and released between the two scrapes, so the
  // cumulative counters move by exactly that and the live total returns.
  EXPECT_EQ(*allocations_after, *allocations_before + 1);
  EXPECT_EQ(*bytes_after, *bytes_before + static_cast<double>(kBlockBytes));
}

#if defined(NDEBUG)

// A release build counts the violation and traces it, and the guarded code
// keeps running: SPEC 8.3 point 5 puts the decision to stop in a debug build.
// It is also the only build in which the metric that carries K10 can be seen
// moving, because in a debug build the first violation ends the process.
// Room for a text dump of the allocation metrics, which is six short lines.
constexpr std::size_t kDumpBytes = 1024;

TEST(AllocationMetricsTest, CountsAGuardViolationIntoTheMetricThatCarriesK10) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  metrics::MetricRegistry registry;
  AllocationMetrics published;
  ASSERT_TRUE(published.register_with(registry).has_value());
  published.refresh();
  const std::uint64_t before = AllocationTracker::instance().process_stats().violation_count;

  {
    const no_alloc_scope guard;
    const ByteBlock forbidden(kBlockBytes);
    EXPECT_EQ(forbidden.size(), kBlockBytes);
  }

  published.refresh();
  std::array<char, kDumpBytes> buffer{};
  metrics::TextExporter dump{buffer};
  registry.collect(dump);

  const std::uint64_t after = AllocationTracker::instance().process_stats().violation_count;
  EXPECT_EQ(after, before + 1);
  EXPECT_NE(dump.view().find("volt_alloc_violations_total"), std::string_view::npos);
  EXPECT_NE(dump.view().find(std::to_string(after)), std::string_view::npos)
      << "the metric has to carry the count the tracker holds:\n"
      << dump.view();
}

TEST(NoAllocScopeTest, CountsTheViolationAndKeepsRunningInARelease) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  std::uint64_t violations = 0;
  std::size_t block_bytes = 0;
  {
    const no_alloc_scope guard;
    const ByteBlock block(kBlockBytes);
    block_bytes = block.size();
    violations = guard.violations();
  }

  EXPECT_EQ(violations, 1U);
  EXPECT_EQ(block_bytes, kBlockBytes);
  EXPECT_GE(AllocationTracker::current_thread_stats().violation_count, 1U);
}

#else

// A debug build stops at the first forbidden allocation, which is the whole
// point of the guard: the stack that allocated is still on the stack.
TEST(NoAllocScopeDeathTest, EndsTheProcessAtTheFirstAllocationInADebugBuild) {
  if (sanitizer_owns_heap()) {
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  EXPECT_DEATH(
      {
        const no_alloc_scope guard;
        const ByteBlock block(kBlockBytes);
      },
      "inside a no_alloc_scope");
}

#endif

} // namespace
} // namespace volt::memory
