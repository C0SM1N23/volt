#include "volt/memory/alignment.hpp"
#include "volt/memory/arena.hpp"
#include "volt/memory/atomic_fixed_pool.hpp"
#include "volt/memory/bounded_queue.hpp"
#include "volt/memory/byte_count.hpp"
#include "volt/memory/fixed_pool.hpp"
#include "volt/memory/frame_scope.hpp"
#include "volt/memory/mpsc_bounded_queue.hpp"
#include "volt/memory/pool_index.hpp"
#include "volt/memory/seq_lock.hpp"

#include "volt/core/error_code.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace volt::memory {
namespace {

struct Sample final {
  std::uint32_t sequence;
  std::uint32_t value;

  [[nodiscard]] bool operator==(const Sample &) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<Sample>);
static_assert(!std::is_convertible_v<std::size_t, ByteCount>);
static_assert(!std::is_convertible_v<std::uint32_t, PoolIndex>);

TEST(FixedPoolTest, ReusesReleasedSlotWithoutChangingCapacity) {
  FixedPool<Sample, 2> pool;

  const core::expected<PoolIndex> first = pool.allocate();
  const core::expected<PoolIndex> second = pool.allocate();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(*first, *second);
  EXPECT_EQ(pool.available(), 0U);
  EXPECT_EQ(pool.capacity(), 2U);

  const core::expected<PoolIndex> exhausted = pool.allocate();
  ASSERT_FALSE(exhausted.has_value());
  EXPECT_EQ(exhausted.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(pool.allocation_failures(), 1U);

  ASSERT_TRUE(pool.release(*first).has_value());
  const core::expected<PoolIndex> reused = pool.allocate();
  ASSERT_TRUE(reused.has_value());
  EXPECT_EQ(*reused, *first);
}

TEST(FixedPoolTest, RejectsForeignAndReleasedIndices) {
  FixedPool<Sample, 2> pool;
  const PoolIndex foreign{9U};

  const core::expected<std::reference_wrapper<Sample>> missing = pool.get(foreign);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), core::ErrorCode::kInternalOutOfRange);
  EXPECT_EQ(pool.access_failures(), 1U);

  const core::expected<PoolIndex> index = pool.allocate();
  ASSERT_TRUE(index.has_value());
  ASSERT_TRUE(pool.release(*index).has_value());
  const core::expected<void> duplicate = pool.release(*index);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), core::ErrorCode::kInternalOutOfRange);
  EXPECT_EQ(pool.release_failures(), 1U);
}

TEST(FixedPoolTest, ProvidesMutableAndConstAccessToOwnedSlot) {
  FixedPool<Sample, 1> pool;
  const core::expected<PoolIndex> index = pool.allocate();
  ASSERT_TRUE(index.has_value());

  core::expected<std::reference_wrapper<Sample>> mutable_value = pool.get(*index);
  ASSERT_TRUE(mutable_value.has_value());
  mutable_value->get() = Sample{.sequence = 4U, .value = 8U};

  const FixedPool<Sample, 1> &const_pool = pool;
  const core::expected<std::reference_wrapper<const Sample>> immutable_value =
      const_pool.get(*index);
  ASSERT_TRUE(immutable_value.has_value());
  EXPECT_EQ(immutable_value->get(), (Sample{.sequence = 4U, .value = 8U}));
}

TEST(AtomicFixedPoolTest, ReportsCapacityAccessAndErrors) {
  AtomicFixedPool<Sample, 1> pool;
  const core::expected<PoolIndex> index = pool.allocate();
  ASSERT_TRUE(index.has_value());
  EXPECT_EQ(pool.capacity(), 1U);
  EXPECT_EQ(pool.available(), 0U);

  core::expected<std::reference_wrapper<Sample>> value = pool.get(*index);
  ASSERT_TRUE(value.has_value());
  value->get() = Sample{.sequence = 2U, .value = 3U};
  const AtomicFixedPool<Sample, 1> &const_pool = pool;
  const core::expected<std::reference_wrapper<const Sample>> read = const_pool.get(*index);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->get(), (Sample{.sequence = 2U, .value = 3U}));

  const core::expected<PoolIndex> exhausted = pool.allocate();
  ASSERT_FALSE(exhausted.has_value());
  EXPECT_EQ(exhausted.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(pool.allocation_failures(), 1U);
  EXPECT_EQ(pool.access_failures(), 0U);

  ASSERT_TRUE(pool.release(*index).has_value());
  const core::expected<void> duplicate = pool.release(*index);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(pool.release_failures(), 1U);
  const core::expected<std::reference_wrapper<Sample>> released_value = pool.get(*index);
  ASSERT_FALSE(released_value.has_value());
  EXPECT_EQ(released_value.error(), core::ErrorCode::kInternalOutOfRange);
  EXPECT_EQ(pool.access_failures(), 1U);
}

TEST(AlignmentTest, AcceptsOnlyNonzeroPowersOfTwo) {
  const core::expected<Alignment> valid = Alignment::create(ByteCount::from_bytes(16U));
  ASSERT_TRUE(valid.has_value());
  EXPECT_EQ(valid->bytes(), ByteCount::from_bytes(16U));

  const core::expected<Alignment> zero = Alignment::create(ByteCount{});
  const core::expected<Alignment> non_power = Alignment::create(ByteCount::from_bytes(3U));
  ASSERT_FALSE(zero.has_value());
  ASSERT_FALSE(non_power.has_value());
  EXPECT_EQ(zero.error(), core::ErrorCode::kConfigValueOutOfRange);
  EXPECT_EQ(non_power.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TEST(ArenaTest, AllocatesWithDefaultAndRequestedAlignmentThenResets) {
  std::array<std::byte, 128> storage{};
  const core::expected<Alignment> default_alignment = Alignment::create(ByteCount::from_bytes(8U));
  const core::expected<Alignment> wider_alignment = Alignment::create(ByteCount::from_bytes(32U));
  ASSERT_TRUE(default_alignment.has_value());
  ASSERT_TRUE(wider_alignment.has_value());

  Arena arena{storage, *default_alignment};
  EXPECT_EQ(arena.capacity_bytes(), ByteCount::from_bytes(storage.size()));
  EXPECT_EQ(arena.default_alignment().bytes(), ByteCount::from_bytes(8U));

  const core::expected<std::span<std::byte>> first = arena.allocate(ByteCount::from_bytes(3U));
  const core::expected<std::span<std::byte>> second =
      arena.allocate(ByteCount::from_bytes(7U), *wider_alignment);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->size(), 3U);
  EXPECT_EQ(second->size(), 7U);
  EXPECT_GT(arena.used_bytes(), ByteCount::from_bytes(10U));

  arena.reset();
  EXPECT_EQ(arena.used_bytes(), ByteCount{});
}

TEST(ArenaTest, CountsAllocationsThatDoNotFit) {
  std::array<std::byte, 8> storage{};
  const core::expected<Alignment> alignment = Alignment::create(ByteCount::from_bytes(8U));
  ASSERT_TRUE(alignment.has_value());
  Arena arena{storage, *alignment};

  const core::expected<std::span<std::byte>> allocation =
      arena.allocate(ByteCount::from_bytes(storage.size() + 1U));
  ASSERT_FALSE(allocation.has_value());
  EXPECT_EQ(allocation.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(arena.allocation_failures(), 1U);
}

TEST(FrameScopeTest, RewindsOnlyAllocationsMadeInsideItsFrame) {
  std::array<std::byte, 64> storage{};
  const core::expected<Alignment> alignment = Alignment::create(ByteCount::from_bytes(8U));
  ASSERT_TRUE(alignment.has_value());
  Arena arena{storage, *alignment};

  ASSERT_TRUE(arena.allocate(ByteCount::from_bytes(8U)).has_value());
  const ByteCount before_frame = arena.used_bytes();
  {
    FrameScope frame{arena};
    ASSERT_TRUE(arena.allocate(ByteCount::from_bytes(16U)).has_value());
    EXPECT_GT(arena.used_bytes(), before_frame);
  }
  EXPECT_EQ(arena.used_bytes(), before_frame);
}

TEST(BoundedQueueTest, PreservesFifoAndCountsFullAndEmptyResults) {
  BoundedQueue<std::uint32_t, 2> queue;
  EXPECT_EQ(queue.capacity(), 2U);
  ASSERT_TRUE(queue.try_push(11U).has_value());
  ASSERT_TRUE(queue.try_push(12U).has_value());

  const core::expected<void> full = queue.try_push(13U);
  ASSERT_FALSE(full.has_value());
  EXPECT_EQ(full.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(queue.push_failures(), 1U);

  const core::expected<std::uint32_t> first = queue.try_pop();
  const core::expected<std::uint32_t> second = queue.try_pop();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, 11U);
  EXPECT_EQ(*second, 12U);

  const core::expected<std::uint32_t> empty = queue.try_pop();
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), core::ErrorCode::kResourceUnavailable);
  EXPECT_EQ(queue.pop_failures(), 1U);
}

TEST(MpscBoundedQueueTest, PreservesFifoAndCountsFullAndEmptyResults) {
  MpscBoundedQueue<std::uint32_t, 2> queue;
  EXPECT_EQ(queue.capacity(), 2U);
  ASSERT_TRUE(queue.try_push(21U).has_value());
  ASSERT_TRUE(queue.try_push(22U).has_value());

  const core::expected<void> full = queue.try_push(23U);
  ASSERT_FALSE(full.has_value());
  EXPECT_EQ(full.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(queue.push_failures(), 1U);

  const core::expected<std::uint32_t> first = queue.try_pop();
  const core::expected<std::uint32_t> second = queue.try_pop();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, 21U);
  EXPECT_EQ(*second, 22U);

  const core::expected<std::uint32_t> empty = queue.try_pop();
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), core::ErrorCode::kResourceUnavailable);
  EXPECT_EQ(queue.pop_failures(), 1U);
}

TEST(SeqLockTest, ReturnsTheLatestCompleteValue) {
  SeqLock<Sample> lock;
  const core::expected<Sample> initial = lock.load();
  ASSERT_TRUE(initial.has_value());
  EXPECT_EQ(*initial, (Sample{.sequence = 0U, .value = 0U}));

  lock.store(Sample{.sequence = 7U, .value = 19U});
  const core::expected<Sample> loaded = lock.load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(*loaded, (Sample{.sequence = 7U, .value = 19U}));
  EXPECT_EQ(lock.read_failures(), 0U);
}

} // namespace
} // namespace volt::memory
