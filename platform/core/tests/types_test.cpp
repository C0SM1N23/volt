#include "volt/core/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace volt::core {
namespace {

// The reason the strong types exist is that these conversions do not exist.
// Asserting it here means a regression fails to compile rather than failing a
// review.
static_assert(std::is_constructible_v<NodeId, std::uint32_t>);
static_assert(!std::is_convertible_v<std::uint32_t, NodeId>);
static_assert(!std::is_convertible_v<NodeId, std::uint32_t>);
static_assert(!std::is_convertible_v<NodeId, ServiceId>);
static_assert(!std::is_convertible_v<ServiceId, NodeId>);

// The same guarantee stated the other way round: an assignment between two
// identifier kinds must not be a valid expression.
template <typename To, typename From>
concept AssignableFrom = requires(To target, From source) { target = source; };

static_assert(AssignableFrom<NodeId, NodeId>);
static_assert(!AssignableFrom<NodeId, ServiceId>);
static_assert(!AssignableFrom<NodeId, std::uint32_t>);
static_assert(!AssignableFrom<Epoch, NodeId>);

// Widths are load-bearing, not incidental: Epoch must not wrap and Priority
// must map onto POSIX real-time priorities.
static_assert(std::is_same_v<Epoch::underlying_type, std::uint64_t>);
static_assert(std::is_same_v<Priority::underlying_type, std::uint8_t>);
static_assert(std::is_same_v<NodeId::underlying_type, std::uint32_t>);

TEST(StrongIdTest, DefaultConstructsToZero) {
  constexpr NodeId identifier{};
  EXPECT_EQ(identifier.value(), 0U);
}

TEST(StrongIdTest, KeepsTheValueItWasConstructedWith) {
  constexpr std::uint32_t kRawValue = 42;
  constexpr NodeId identifier{kRawValue};
  EXPECT_EQ(identifier.value(), kRawValue);
}

TEST(StrongIdTest, IsUsableInConstantExpressions) {
  static_assert(NodeId{7}.value() == 7U);
  static_assert(NodeId{1} < NodeId{2});
  SUCCEED();
}

TEST(StrongIdTest, OrdersByUnderlyingValue) {
  EXPECT_LT(NodeId{1}, NodeId{2});
  EXPECT_GT(NodeId{3}, NodeId{2});
  EXPECT_EQ(NodeId{4}, NodeId{4});
  EXPECT_NE(NodeId{4}, NodeId{5});
}

TEST(StrongIdTest, HashesEqualIdentifiersIdentically) {
  const std::hash<NodeId> hasher;
  EXPECT_EQ(hasher(NodeId{9}), hasher(NodeId{9}));
}

TEST(StrongIdTest, WorksAsAnUnorderedContainerKey) {
  std::unordered_set<NodeId> nodes;
  nodes.insert(NodeId{1});
  nodes.insert(NodeId{2});
  nodes.insert(NodeId{1});
  EXPECT_EQ(nodes.size(), 2U);
  EXPECT_TRUE(nodes.contains(NodeId{2}));
  EXPECT_FALSE(nodes.contains(NodeId{3}));
}

TEST(EpochTest, HoldsValuesBeyondThirtyTwoBits) {
  constexpr std::uint64_t kBeyondThirtyTwoBits = 0x1'0000'0000ULL;
  constexpr Epoch epoch{kBeyondThirtyTwoBits};
  EXPECT_EQ(epoch.value(), kBeyondThirtyTwoBits);
  EXPECT_GT(epoch, Epoch{0xFFFF'FFFFULL});
}

TEST(PriorityTest, OrdersSoThatALargerValueIsMoreUrgent) {
  // The values are the POSIX real-time priorities SPEC 42.2 assigns to the
  // timer thread and to the control thread.
  constexpr Priority kTimerThread{95};
  constexpr Priority kControlThread{90};
  EXPECT_GT(kTimerThread, kControlThread);
}

} // namespace
} // namespace volt::core
