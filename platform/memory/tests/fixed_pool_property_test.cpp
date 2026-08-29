#include "volt/memory/atomic_fixed_pool.hpp"
#include "volt/memory/fixed_pool.hpp"
#include "volt/memory/pool_index.hpp"

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace volt::memory {
namespace {

// Thirty-two slots force generated sequences to alternate between the middle
// and both boundaries without making RapidCheck shrinking expensive.
constexpr std::size_t kPoolCapacity = 32;
// Five hundred deterministic cases cover long allocation/release histories,
// changing this changes the property-test runtime and confidence together.
constexpr int kPropertyCases = 500;
// Maximum generated actions per case. Twice the pool capacity reaches both
// exhaustion and reuse repeatedly while retaining small counterexamples.
constexpr int kPropertyMaxActions = 64;
// Fixed seed required by AGENTS.md 8.5. A failure reports this value and its
// shrunk counterexample, so the exact run is reproducible.
constexpr std::uint64_t kPropertySeed = 0x8A37'41C2'09D5'6EF0ULL;

template <typename Pool> void verify_sequence(const std::vector<std::uint8_t> &actions) {
  Pool pool;
  std::vector<PoolIndex> owned;
  owned.reserve(kPoolCapacity);

  for (const std::uint8_t action : actions) {
    const bool should_allocate = owned.empty() || ((action & 1U) == 0U);
    if (should_allocate && owned.size() < kPoolCapacity) {
      const core::expected<PoolIndex> allocated = pool.allocate();
      RC_ASSERT(allocated.has_value());
      RC_ASSERT(std::find(owned.begin(), owned.end(), *allocated) == owned.end());
      owned.push_back(*allocated);
    } else if (!owned.empty()) {
      const std::size_t selected = static_cast<std::size_t>(action) % owned.size();
      RC_ASSERT(pool.release(owned[selected]).has_value());
      owned.erase(owned.begin() + static_cast<std::ptrdiff_t>(selected));
    }
    RC_ASSERT(pool.available() + owned.size() == kPoolCapacity);
  }

  for (const PoolIndex index : owned) {
    RC_ASSERT(pool.release(index).has_value());
  }
  RC_ASSERT(pool.available() == kPoolCapacity);

  std::vector<PoolIndex> all_slots;
  all_slots.reserve(kPoolCapacity);
  for (std::size_t count = 0; count < kPoolCapacity; ++count) {
    const core::expected<PoolIndex> allocated = pool.allocate();
    RC_ASSERT(allocated.has_value());
    RC_ASSERT(std::find(all_slots.begin(), all_slots.end(), *allocated) == all_slots.end());
    all_slots.push_back(*allocated);
  }
  RC_ASSERT(!pool.allocate().has_value());
}

template <typename Property>
void expect_property(Property property, const std::string &identifier) {
  rc::detail::TestMetadata metadata{.id = identifier, .description = identifier};
  rc::detail::TestParams parameters{};
  parameters.seed = kPropertySeed;
  parameters.maxSuccess = kPropertyCases;
  parameters.maxSize = kPropertyMaxActions;

  const rc::detail::TestResult result =
      rc::detail::checkTestable(std::move(property), metadata, parameters);
  std::ostringstream diagnostic;
  rc::detail::printResultMessage(result, diagnostic);
  ASSERT_TRUE(result.is<rc::detail::SuccessResult>()) << "seed=" << kPropertySeed << '\n'
                                                      << diagnostic.str();
}

TEST(FixedPoolPropertyTest, NeverLosesOrDuplicatesSlotsForAnyGeneratedHistory) {
  expect_property(
      [](const std::vector<std::uint8_t> &actions) {
        verify_sequence<FixedPool<std::uint64_t, kPoolCapacity>>(actions);
      },
      "fixed-pool-conservation");
}

TEST(AtomicFixedPoolPropertyTest, NeverLosesOrDuplicatesSlotsForAnyGeneratedHistory) {
  expect_property(
      [](const std::vector<std::uint8_t> &actions) {
        verify_sequence<AtomicFixedPool<std::uint64_t, kPoolCapacity>>(actions);
      },
      "atomic-fixed-pool-conservation");
}

} // namespace
} // namespace volt::memory
