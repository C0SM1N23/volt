// The SPEC 12.2 verification scenario: a 1 kHz producer against a consumer
// that sleeps 50 ms per message while holding the previous sample. The
// producer's cadence must not care.

#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <print>
#include <string>
#include <vector>

namespace volt::ipc {
namespace {

constexpr std::uint64_t kCycles = 2'000;
constexpr auto kPeriod = core::Duration::from_ms(1);
constexpr auto kConsumerNap = core::Duration::from_ms(50);
constexpr TopicConfig kConfig{.slot_count = 16, .history_depth = 4, .max_subscribers = 1};

/// Publish latency budget for the P99 assertion. The path has no lock, no
/// system call and no allocation, so even a preempted sample sits far under
/// this; the bound is loose on purpose because the runner is shared
/// (AGENTS.md 8.9 outlaws tests that flicker with machine load).
constexpr std::int64_t kPublishP99BudgetNs = 100'000;

TEST(IpcSlowConsumerTest, ProducerHoldsItsCadenceWhileTheConsumerSleeps) {
  pal::posix::PosixPlatform platform;
  const std::string name = std::format("volt-ipc-slow-{}", platform.current_process_id());
  core::expected<Topic<std::uint64_t>> topic =
      Topic<std::uint64_t>::create(platform, name, kConfig);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<std::uint64_t>> publisher = topic->publisher();
  core::expected<Subscriber<std::uint64_t>> subscriber = topic->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  std::atomic<bool> done{false};
  std::atomic<std::uint64_t> consumed{0};
  core::expected<std::unique_ptr<pal::IThread>> sleeper =
      platform.create_thread(pal::ThreadConfig{.name = "volt-slow-sub",
                                               .policy = pal::SchedulingPolicy::kOther,
                                               .priority = core::Priority{},
                                               .cpu_mask = 0,
                                               .stack_bytes = 0},
                             [&] {
                               while (!done.load(std::memory_order_acquire)) {
                                 core::expected<Sample<std::uint64_t>> sample = subscriber->take();
                                 if (!sample.has_value()) {
                                   continue;
                                 }
                                 consumed.fetch_add(1, std::memory_order_relaxed);
                                 // The nap happens while the sample is held: the consumer is not
                                 // just slow to poll, it pins pool capacity while it dawdles.
                                 [[maybe_unused]] const core::expected<void> napped =
                                     platform.clock().sleep_for(kConsumerNap);
                               }
                             });
  ASSERT_TRUE(sleeper.has_value());

  pal::IClock &clock = platform.clock();
  std::vector<std::int64_t> publish_ns;
  publish_ns.reserve(kCycles);
  std::uint64_t loan_failures = 0;

  const std::int64_t start_ns = clock.monotonic().ns_since_epoch();
  std::int64_t next_activation_ns = start_ns;
  for (std::uint64_t cycle = 0; cycle < kCycles; ++cycle) {
    const std::int64_t before_ns = clock.monotonic().ns_since_epoch();
    core::expected<Loan<std::uint64_t>> loan = publisher->loan();
    if (!loan.has_value()) {
      loan_failures += 1;
    } else {
      **loan = cycle;
      publisher->publish(std::move(*loan));
    }
    publish_ns.push_back(clock.monotonic().ns_since_epoch() - before_ns);

    next_activation_ns += kPeriod.ns();
    const std::int64_t until_next_ns = next_activation_ns - clock.monotonic().ns_since_epoch();
    if (until_next_ns > 0) {
      [[maybe_unused]] const core::expected<void> paced =
          clock.sleep_for(core::Duration::from_ns(until_next_ns));
    }
  }
  const std::int64_t elapsed_ns = clock.monotonic().ns_since_epoch() - start_ns;
  done.store(true, std::memory_order_release);
  ASSERT_TRUE((*sleeper)->join().has_value());

  // The producer never blocked: every cycle got a slot. The pool cannot
  // starve because the consumer pins at most one held sample plus a full
  // ring: 1 + 4 < 16.
  EXPECT_EQ(loan_failures, 0U);

  // The cadence held: two thousand 1 ms cycles fit their two seconds with
  // only scheduler noise on top. Three seconds of margin would hide a
  // blocking publish of 50 ms - that is the failure this bound exists for.
  EXPECT_LT(elapsed_ns, core::Duration::from_ms(3'000).ns());

  std::ranges::sort(publish_ns);
  const std::int64_t p50 = publish_ns[publish_ns.size() / 2];
  const std::int64_t p99 = publish_ns[publish_ns.size() * 99 / 100];
  const std::int64_t worst = publish_ns.back();
  std::print("slow-consumer publish latency: P50 {} ns, P99 {} ns, max {} ns\n", p50, p99, worst);
  std::print("slow-consumer jitter: {} cycles in {} ms, consumer saw {}, dropped {}\n", kCycles,
             elapsed_ns / 1'000'000, consumed.load(), subscriber->dropped());
  EXPECT_LT(p99, kPublishP99BudgetNs);

  // The slowness was real and every loss is on the books: whatever was
  // published either reached the consumer, waits in the ring, or is counted.
  const std::uint64_t published = kCycles - loan_failures;
  EXPECT_LT(consumed.load(), published / 4) << "the consumer was not slow enough to test anything";
  EXPECT_EQ(consumed.load() + subscriber->dropped() + subscriber->pending(), published);
}

} // namespace
} // namespace volt::ipc
