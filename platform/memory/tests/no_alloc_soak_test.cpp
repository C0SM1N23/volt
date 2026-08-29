#include "volt/memory/alignment.hpp"
#include "volt/memory/allocation_stats.hpp"
#include "volt/memory/allocation_tracker.hpp"
#include "volt/memory/arena.hpp"
#include "volt/memory/bounded_queue.hpp"
#include "volt/memory/byte_count.hpp"
#include "volt/memory/fixed_pool.hpp"
#include "volt/memory/frame_scope.hpp"
#include "volt/memory/no_alloc_scope.hpp"
#include "volt/memory/pool_index.hpp"
#include "volt/memory/seq_lock.hpp"

#include "volt/core/duration.hpp"
#include "volt/core/timestamp.hpp"
#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace volt::memory {
namespace {

// Whether a sanitizer runtime, rather than platform/memory, defines the
// allocation operators in this build. Taken from the build rather than from
// the counters: a tracker that stopped counting must fail this suite, not
// skip it.
[[nodiscard]] constexpr bool sanitizer_owns_heap() noexcept {
#if defined(VOLT_SANITIZER_OWNS_HEAP)
  return true;
#else
  return false;
#endif
}

// The soak length P09 asks for. It is long enough that a lazily initialised
// allocation on a rarely taken path has time to appear, and short enough to
// stay in a per-commit test run. Raising it costs the same time in every
// preset the suite runs under.
constexpr core::Duration kSoakDuration = core::Duration::from_s(60);

// How many cycles run between two clock reads. A control loop that asked the
// clock every iteration would measure the clock rather than the loop.
constexpr std::size_t kCyclesPerClockRead = 4096;

// The shapes below stand in for a real control cycle: a batch of samples
// arrives, is reduced, is published for readers, and its work buffer is
// handed back. Their sizes are what fits a cycle, not a measured requirement.
constexpr std::size_t kQueueCapacity = 64;
constexpr std::size_t kPoolCapacity = 8;
constexpr std::size_t kArenaBytes = 1024;
constexpr std::size_t kFrameBytes = 64;
constexpr std::size_t kSamplesPerCycle = 8;

struct Sample final {
  std::uint64_t sequence;
  std::uint64_t value;
};

/// One control cycle: take samples in, reduce them, publish, hand back.
///
/// Written the way the data plane is meant to be written, so that the guard
/// around it measures the shape of real work rather than an empty loop.
///
/// @rt     allocation-free, which is the property under test
/// @errors whatever a queue, pool or arena reports; every step is sized to
///         succeed, so a failure means the loop stopped being realistic
[[nodiscard]] core::expected<std::uint64_t> run_cycle(BoundedQueue<Sample, kQueueCapacity> &inbox,
                                                      FixedPool<Sample, kPoolCapacity> &pool,
                                                      Arena &arena, SeqLock<Sample> &published,
                                                      std::uint64_t cycle) noexcept {
  const FrameScope frame{arena};
  VOLT_TRY(arena.allocate(ByteCount::from_bytes(kFrameBytes)));

  VOLT_LOOP_BOUND(kSamplesPerCycle);
  for (std::uint64_t index = 0; index < kSamplesPerCycle; ++index) {
    VOLT_TRY(inbox.try_push(Sample{.sequence = cycle, .value = index}));
  }

  std::uint64_t reduced = 0;
  VOLT_LOOP_BOUND(kSamplesPerCycle);
  for (std::uint64_t index = 0; index < kSamplesPerCycle; ++index) {
    const core::expected<Sample> sample = inbox.try_pop();
    VOLT_TRY(sample);
    reduced += sample->value;
  }

  const core::expected<PoolIndex> slot = pool.allocate();
  VOLT_TRY(slot);
  const core::expected<std::reference_wrapper<Sample>> held = pool.get(*slot);
  VOLT_TRY(held);
  held->get() = Sample{.sequence = cycle, .value = reduced};
  VOLT_TRY(pool.release(*slot));

  published.store(Sample{.sequence = cycle, .value = reduced});
  const core::expected<Sample> latest = published.load();
  VOLT_TRY(latest);
  return latest->value;
}

// @verifies K10: no dynamic allocation on the safety path in steady state
TEST(NoAllocSoakTest, RunsAControlLoopForAMinuteWithoutAllocating) {
  if (sanitizer_owns_heap()) {
    // Without VOLT's allocator the counters never move, so the loop would
    // report zero allocations whether or not it made any.
    GTEST_SKIP() << "a sanitizer runtime owns operator new in this build";
  }

  pal::posix::PosixPlatform platform;
  BoundedQueue<Sample, kQueueCapacity> inbox;
  FixedPool<Sample, kPoolCapacity> pool;
  SeqLock<Sample> published;
  std::array<std::byte, kArenaBytes> storage{};
  const core::expected<Alignment> alignment = Alignment::create(ByteCount::from_bytes(16U));
  ASSERT_TRUE(alignment.has_value());
  Arena arena{storage, *alignment};

  const core::Timestamp start = platform.clock().monotonic();
  const AllocationStats before = AllocationTracker::current_thread_stats();

  std::uint64_t cycles = 0;
  std::uint64_t published_total = 0;
  std::uint64_t violations = 0;
  bool every_cycle_ran = true;
  bool elapsed = false;
  {
    const no_alloc_scope guard;
    while (!elapsed) {
      VOLT_LOOP_BOUND(kCyclesPerClockRead);
      for (std::size_t batch = 0; batch < kCyclesPerClockRead; ++batch) {
        const core::expected<std::uint64_t> value =
            run_cycle(inbox, pool, arena, published, cycles);
        every_cycle_ran = every_cycle_ran && value.has_value();
        published_total += value.value_or(0);
        ++cycles;
      }
      const core::expected<core::Duration> ran = platform.clock().monotonic().checked_since(start);
      elapsed = !ran.has_value() || *ran >= kSoakDuration;
    }
    violations = guard.violations();
  }

  const AllocationStats after = AllocationTracker::current_thread_stats();

  EXPECT_TRUE(every_cycle_ran);
  EXPECT_EQ(violations, 0U);
  EXPECT_EQ(after.allocation_count, before.allocation_count);
  EXPECT_EQ(after.violation_count, before.violation_count);
  EXPECT_GT(cycles, 0U);
  EXPECT_GT(published_total, 0U);
}

} // namespace
} // namespace volt::memory
