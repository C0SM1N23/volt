#include "volt/ipc/detail/free_list.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <vector>

namespace volt::ipc::detail {
namespace {

constexpr std::size_t kSlots = 8;

class FreeListTest : public ::testing::Test {
protected:
  FreeListTest() { list().initialise(); }

  FreeList list() { return FreeList{head_, available_, next_}; }

  std::atomic<std::uint64_t> head_{};
  std::atomic<std::uint64_t> available_{};
  std::array<std::atomic<std::uint32_t>, kSlots> next_{};
};

TEST_F(FreeListTest, HandsOutEveryIndexExactlyOnce) {
  std::set<std::uint32_t> seen;
  for (std::size_t round = 0; round < kSlots; ++round) {
    const core::expected<std::uint32_t> index = list().allocate();
    ASSERT_TRUE(index.has_value());
    EXPECT_LT(*index, kSlots);
    EXPECT_TRUE(seen.insert(*index).second) << "index handed out twice";
  }
  EXPECT_EQ(seen.size(), kSlots);
  EXPECT_EQ(list().available(), 0U);
}

TEST_F(FreeListTest, ReportsExhaustionWhenEverySlotIsOwned) {
  for (std::size_t round = 0; round < kSlots; ++round) {
    ASSERT_TRUE(list().allocate().has_value());
  }
  const core::expected<std::uint32_t> extra = list().allocate();
  ASSERT_FALSE(extra.has_value());
  EXPECT_EQ(extra.error(), core::ErrorCode::kResourceExhausted);
}

TEST_F(FreeListTest, AReleasedSlotCanBeAllocatedAgain) {
  const core::expected<std::uint32_t> first = list().allocate();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(list().release(*first).has_value());
  EXPECT_EQ(list().available(), kSlots);

  // LIFO: the released slot is the next one out, which keeps hot cache
  // lines hot; asserting it pins the stack discipline.
  const core::expected<std::uint32_t> second = list().allocate();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, *first);
}

TEST_F(FreeListTest, RejectsAForeignIndex) {
  const core::expected<void> released = list().release(kSlots);
  ASSERT_FALSE(released.has_value());
  EXPECT_EQ(released.error(), core::ErrorCode::kInternalOutOfRange);
}

TEST_F(FreeListTest, TracksAvailabilityThroughChurn) {
  std::vector<std::uint32_t> held;
  for (std::size_t round = 0; round < kSlots / 2; ++round) {
    const core::expected<std::uint32_t> index = list().allocate();
    ASSERT_TRUE(index.has_value());
    held.push_back(*index);
  }
  EXPECT_EQ(list().available(), kSlots - held.size());
  for (const std::uint32_t index : held) {
    ASSERT_TRUE(list().release(index).has_value());
  }
  EXPECT_EQ(list().available(), kSlots);
}

} // namespace
} // namespace volt::ipc::detail
