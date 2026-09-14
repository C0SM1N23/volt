// Proof that the intra-node path moves no payload bytes: the linker routes
// every memcpy call through the counter below (--wrap=memcpy), the test
// window covers a thousand publish/take cycles, and the count must stay
// exactly where it started. The instrument itself is proven live first, so
// a broken wrap cannot green the test by silence.

#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>

namespace {

std::atomic<std::uint64_t> memcpy_calls{0};

} // namespace

extern "C" {
// The two names are the linker's --wrap contract, not a choice; the reserved
// prefix is imposed the same way the section symbols of DEV-005 are.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
[[nodiscard]] void *__real_memcpy(void *destination, const void *source, std::size_t bytes);

/// Every call the compiler or the code emits to `memcpy` lands here.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
[[nodiscard]] void *__wrap_memcpy(void *destination, const void *source, std::size_t bytes) {
  memcpy_calls.fetch_add(1, std::memory_order_relaxed);
  return __real_memcpy(destination, source, bytes);
}
}

namespace volt::ipc {
namespace {

struct BrakeCommand {
  std::uint64_t sequence = 0;
  float pressure = 0.0F;
  float slew_rate = 0.0F;
};

constexpr std::uint64_t kCycles = 1'000;
constexpr TopicConfig kConfig{.slot_count = 8, .history_depth = 4, .max_subscribers = 1};

TEST(IpcZeroCopyTest, TheCounterItselfCounts) {
  const std::uint64_t before = memcpy_calls.load(std::memory_order_relaxed);
  std::uint64_t source = 0xAB;
  std::uint64_t destination = 0;
  static_cast<void>(std::memcpy(&destination, &source, sizeof(source)));
  EXPECT_GT(memcpy_calls.load(std::memory_order_relaxed), before)
      << "the wrap is dead and every zero below would be a lie";
  EXPECT_EQ(destination, 0xABU);
}

TEST(IpcZeroCopyTest, PublishAndTakeMoveNoPayloadBytes) {
  pal::posix::PosixPlatform platform;
  const std::string name = std::format("volt-ipc-zc-{}", platform.current_process_id());
  core::expected<Topic<BrakeCommand>> topic = Topic<BrakeCommand>::create(platform, name, kConfig);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<BrakeCommand>> publisher = topic->publisher();
  core::expected<Subscriber<BrakeCommand>> subscriber = topic->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  std::uint64_t checksum = 0;
  const std::uint64_t before = memcpy_calls.load(std::memory_order_relaxed);
  for (std::uint64_t cycle = 0; cycle < kCycles; ++cycle) {
    core::expected<Loan<BrakeCommand>> loan = publisher->loan();
    ASSERT_TRUE(loan.has_value());
    // Built in place, in the shared slot; published as an index; read where
    // it was written. Nothing below may copy the payload.
    (*loan)->sequence = cycle;
    (*loan)->pressure = 2.5F;
    publisher->publish(std::move(*loan));

    core::expected<Sample<BrakeCommand>> sample = subscriber->take();
    ASSERT_TRUE(sample.has_value());
    checksum += (*sample)->sequence;
  }
  const std::uint64_t after = memcpy_calls.load(std::memory_order_relaxed);

  EXPECT_EQ(after - before, 0U) << "the zero-copy path copied";
  EXPECT_EQ(checksum, kCycles * (kCycles - 1) / 2);
}

} // namespace
} // namespace volt::ipc
