#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

namespace volt::ipc {
namespace {

/// Must match the helper's payload.
struct CrashPayload {
  std::uint64_t sequence = 0;
};

constexpr std::uint32_t kHostages = 100;
constexpr TopicConfig kConfig{.slot_count = 160, .history_depth = 8, .max_subscribers = 2};

/// The parent gives the child this long to attach and take its hostages
/// before declaring the test broken, in publish attempts, not wall time.
constexpr std::uint64_t kPublishAttempts = 2'000'000;

TEST(IpcCrashTest, PoolRecoversEverySlotAfterKillMinusNine) {
  pal::posix::PosixPlatform platform;
  const std::string name = std::format("volt-ipc-crash-{}", platform.current_process_id());
  core::expected<Topic<CrashPayload>> topic = Topic<CrashPayload>::create(platform, name, kConfig);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<CrashPayload>> publisher = topic->publisher();
  ASSERT_TRUE(publisher.has_value());

  const std::array<std::string_view, 2> arguments{name, "100"};
  const core::expected<std::unique_ptr<pal::IProcess>> consumer = platform.spawn_process(
      pal::ProcessConfig{.executable = VOLT_IPC_CRASH_CONSUMER, .arguments = arguments});
  ASSERT_TRUE(consumer.has_value());

  // Feed the child until the pool shows its hostages: one hundred samples
  // held in another process, never to be released voluntarily.
  bool hostages_taken = false;
  std::uint64_t sequence = 0;
  for (std::uint64_t attempt = 0; attempt < kPublishAttempts && !hostages_taken; ++attempt) {
    core::expected<Loan<CrashPayload>> loan = publisher->loan();
    if (loan.has_value()) {
      (*loan)->sequence = sequence;
      sequence += 1;
      publisher->publish(std::move(*loan));
    }
    hostages_taken = topic->available_slots() <= kConfig.slot_count - kHostages;
  }
  ASSERT_TRUE(hostages_taken) << "the child never took its hostages";

  // kill -9: no destructors, no goodbyes. The child's seat, its pending
  // ring entries and its hundred held samples are now garbage with a dead
  // owner recorded on them.
  ASSERT_TRUE((*consumer)->kill().has_value());
  const core::expected<pal::ProcessExit> exit = (*consumer)->wait();
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->reason, pal::ExitReason::kSignalled);

  // Recovery runs on the next seat claim; a fresh subscriber both proves the
  // seat came back and drains whatever the recovery returned.
  core::expected<Subscriber<CrashPayload>> successor = topic->subscriber();
  ASSERT_TRUE(successor.has_value());
  EXPECT_EQ(topic->available_slots(), kConfig.slot_count)
      << "a slot leaked past the crash recovery";

  // The producer, which never stopped existing, keeps working: publish
  // through a full pool's worth of slots and read everything back.
  std::uint64_t verified = 0;
  for (std::uint32_t message = 0; message < kConfig.slot_count; ++message) {
    core::expected<Loan<CrashPayload>> loan = publisher->loan();
    ASSERT_TRUE(loan.has_value());
    (*loan)->sequence = 7'000 + message;
    publisher->publish(std::move(*loan));
    const core::expected<Sample<CrashPayload>> sample = successor->take();
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ((*sample)->sequence, 7'000U + message);
    verified += 1;
  }
  EXPECT_EQ(verified, kConfig.slot_count);
  EXPECT_EQ(topic->available_slots(), kConfig.slot_count);
}

} // namespace
} // namespace volt::ipc
