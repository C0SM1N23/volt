#include "volt/memory/bounded_queue.hpp"

#include "volt/core/error_code.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>

namespace volt::memory {
namespace {

// Ten million interleavings are required by P08. Lowering this weakens the
// model campaign; increasing it raises every CI job's deterministic runtime.
constexpr std::size_t kOperationCount = 10'000'000;
// The reference capacity matches the queue under test. A small power of two
// makes full and empty transitions common in a random campaign.
constexpr std::size_t kQueueCapacity = 64;
// Fixed seed required by AGENTS.md 8.5 and printed by SCOPED_TRACE on failure.
constexpr std::uint64_t kModelSeed = 0x47C9'81A5'D306'2BEFULL;

class ReferenceQueue final {
public:
  [[nodiscard]] bool push(std::uint64_t value) {
    const std::scoped_lock lock{mutex_};
    if (values_.size() == kQueueCapacity) {
      return false;
    }
    values_.push_back(value);
    return true;
  }

  [[nodiscard]] core::expected<std::uint64_t> pop() {
    const std::scoped_lock lock{mutex_};
    if (values_.empty()) {
      return std::unexpected{core::ErrorCode::kResourceUnavailable};
    }
    const std::uint64_t value = values_.front();
    values_.pop_front();
    return value;
  }

private:
  std::mutex mutex_;
  std::deque<std::uint64_t> values_;
};

[[nodiscard]] bool apply_push(BoundedQueue<std::uint64_t, kQueueCapacity> &queue,
                              ReferenceQueue &reference, std::uint64_t value) {
  const bool reference_pushed = reference.push(value);
  const core::expected<void> queue_pushed = queue.try_push(value);
  return reference_pushed == queue_pushed.has_value() &&
         (queue_pushed.has_value() || queue_pushed.error() == core::ErrorCode::kResourceExhausted);
}

[[nodiscard]] bool apply_pop(BoundedQueue<std::uint64_t, kQueueCapacity> &queue,
                             ReferenceQueue &reference) {
  const core::expected<std::uint64_t> reference_value = reference.pop();
  const core::expected<std::uint64_t> queue_value = queue.try_pop();
  return reference_value == queue_value;
}

TEST(BoundedQueueModelTest, MatchesMutexProtectedReferenceForTenMillionInterleavings) {
  SCOPED_TRACE(::testing::Message{} << "seed=" << kModelSeed);
  BoundedQueue<std::uint64_t, kQueueCapacity> queue;
  ReferenceQueue reference;
  std::mt19937_64 random{kModelSeed};

  VOLT_LOOP_BOUND(kOperationCount);
  for (std::size_t operation = 0; operation < kOperationCount; ++operation) {
    const std::uint64_t decision = random();
    const bool matched = (decision & 1U) == 0U ? apply_push(queue, reference, decision)
                                               : apply_pop(queue, reference);
    ASSERT_TRUE(matched) << "seed=" << kModelSeed << " operation=" << operation;
  }
}

} // namespace
} // namespace volt::memory
