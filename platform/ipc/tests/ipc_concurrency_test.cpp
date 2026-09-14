#include "volt/ipc/detail/free_list.hpp"
#include "volt/ipc/detail/index_ring.hpp"
#include "volt/ipc/detail/topic_state.hpp"
#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace volt::ipc {
namespace {

[[nodiscard]] constexpr pal::ThreadConfig thread_config(std::string_view name) noexcept {
  // Normal priority, inherited CPU set, default stack: a load test wants the
  // scheduler's worst ordinary behaviour, not a tuned lane.
  return pal::ThreadConfig{.name = name,
                           .policy = pal::SchedulingPolicy::kOther,
                           .priority = core::Priority{},
                           .cpu_mask = 0,
                           .stack_bytes = 0};
}

/// Marks used by the conservation ledgers below.
constexpr std::uint8_t kSeenByConsumer = 1;
constexpr std::uint8_t kSeenAsEvicted = 2;

TEST(IpcConcurrencyTest, RingHandsEachEntryToExactlyOneSide) {
  constexpr std::uint32_t kMessages = 500'000;
  constexpr std::uint32_t kDepth = 64;
  constexpr std::size_t kCapacity = 64;

  std::atomic<std::uint64_t> head{};
  std::atomic<std::uint64_t> tail{};
  std::array<std::atomic<std::uint32_t>, kCapacity> cells{};
  std::atomic<std::uint64_t> stage{detail::kStageEmpty};
  detail::IndexRing ring{head, tail, cells, kDepth};

  // One byte per message: who ended up owning it. Any message owned twice
  // or never is a broken handover.
  std::vector<std::atomic<std::uint8_t>> ledger(kMessages);
  std::atomic<bool> producer_done{false};

  pal::posix::PosixPlatform platform;
  core::expected<std::unique_ptr<pal::IThread>> consumer =
      platform.create_thread(thread_config("volt-ring-cons"), [&] {
        while (true) {
          const core::expected<std::uint32_t> value = ring.pop(stage);
          if (value.has_value()) {
            stage.store(detail::kStageEmpty, std::memory_order_release);
            const std::uint8_t previous =
                ledger[*value].fetch_or(kSeenByConsumer, std::memory_order_relaxed);
            EXPECT_EQ(previous, 0U) << "entry " << *value << " owned twice";
            continue;
          }
          if (producer_done.load(std::memory_order_acquire)) {
            // One last sweep: everything pushed before the flag flipped is
            // visible to this pop loop by now.
            while (const core::expected<std::uint32_t> last = ring.pop(stage)) {
              stage.store(detail::kStageEmpty, std::memory_order_release);
              const std::uint8_t previous =
                  ledger[*last].fetch_or(kSeenByConsumer, std::memory_order_relaxed);
              EXPECT_EQ(previous, 0U);
            }
            return;
          }
        }
      });
  ASSERT_TRUE(consumer.has_value());

  for (std::uint32_t message = 0; message < kMessages; ++message) {
    const core::expected<detail::PushOutcome> outcome = ring.push(message, stage);
    ASSERT_TRUE(outcome.has_value());
    const std::optional<std::uint32_t> evicted = outcome->evicted;
    if (evicted.has_value()) {
      const std::uint8_t previous =
          ledger[*evicted].fetch_or(kSeenAsEvicted, std::memory_order_relaxed);
      EXPECT_EQ(previous, 0U) << "entry " << *evicted << " owned twice";
    }
  }
  producer_done.store(true, std::memory_order_release);
  ASSERT_TRUE((*consumer)->join().has_value());

  std::uint64_t consumed = 0;
  std::uint64_t evicted = 0;
  for (std::uint32_t message = 0; message < kMessages; ++message) {
    const std::uint8_t mark = ledger[message].load(std::memory_order_relaxed);
    ASSERT_TRUE(mark == kSeenByConsumer || mark == kSeenAsEvicted)
        << "entry " << message << " ended nowhere or on both sides";
    consumed += (mark == kSeenByConsumer) ? 1U : 0U;
    evicted += (mark == kSeenAsEvicted) ? 1U : 0U;
  }
  EXPECT_EQ(consumed + evicted, kMessages);
  EXPECT_GT(consumed, 0U);
}

TEST(IpcConcurrencyTest, FreeListNeverHandsOneSlotToTwoThreads) {
  constexpr std::size_t kSlots = 64;
  constexpr std::size_t kWorkers = 4;
  constexpr std::uint64_t kTransfersPerWorker = 250'000;

  std::atomic<std::uint64_t> head{};
  std::atomic<std::uint64_t> available{};
  std::array<std::atomic<std::uint32_t>, kSlots> next{};
  detail::FreeList list{head, available, next};
  list.initialise();

  std::array<std::atomic<std::uint8_t>, kSlots> claimed{};
  pal::posix::PosixPlatform platform;
  std::vector<std::unique_ptr<pal::IThread>> workers;
  std::atomic<std::uint64_t> ownership_violations{};

  for (std::size_t worker = 0; worker < kWorkers; ++worker) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        platform.create_thread(thread_config("volt-fl-worker"), [&] {
          for (std::uint64_t transfer = 0; transfer < kTransfersPerWorker; ++transfer) {
            const core::expected<std::uint32_t> index = list.allocate();
            if (!index.has_value()) {
              // Exhaustion and contention are legal answers under load; the
              // property being hammered is exclusivity, not availability.
              continue;
            }
            if (claimed[*index].exchange(1, std::memory_order_acq_rel) != 0) {
              ownership_violations.fetch_add(1, std::memory_order_relaxed);
            }
            claimed[*index].store(0, std::memory_order_release);
            // Contention is an answer, not a failure: every lost race means
            // another worker won one, so the caller retries. Four workers on
            // a two-core runner reach that path often enough that treating
            // it as fatal is what would be wrong. Exactly one release per
            // claimed slot, however many attempts it takes.
            core::expected<void> released = list.release(*index);
            while (!released.has_value() && released.error() == core::ErrorCode::kResourceBusy) {
              released = list.release(*index);
            }
            EXPECT_TRUE(released.has_value()) << "a slot could not be returned at all";
          }
        });
    ASSERT_TRUE(thread.has_value());
    workers.push_back(std::move(*thread));
  }
  for (std::unique_ptr<pal::IThread> &worker : workers) {
    ASSERT_TRUE(worker->join().has_value());
  }
  EXPECT_EQ(ownership_violations.load(std::memory_order_relaxed), 0U);
  EXPECT_EQ(list.available(), kSlots) << "a slot never made it back to the pool";
}

TEST(IpcConcurrencyTest, TopicConservesEveryMessageUnderLoad) {
  constexpr std::uint64_t kMessages = 200'000;
  constexpr std::uint32_t kSubscriberCount = 3;
  constexpr TopicConfig kConfig{
      .slot_count = 256, .history_depth = 64, .max_subscribers = kSubscriberCount};

  pal::posix::PosixPlatform platform;
  const std::string name = std::format("volt-ipc-conserve-{}", platform.current_process_id());
  core::expected<Topic<std::uint64_t>> topic =
      Topic<std::uint64_t>::create(platform, name, kConfig);
  ASSERT_TRUE(topic.has_value());

  core::expected<Publisher<std::uint64_t>> publisher = topic->publisher();
  ASSERT_TRUE(publisher.has_value());

  // Every seat is claimed before the first publish, so each port's ledger
  // starts at message zero and the conservation equation has no grace terms.
  std::vector<Subscriber<std::uint64_t>> subscribers;
  for (std::uint32_t seat = 0; seat < kSubscriberCount; ++seat) {
    core::expected<Subscriber<std::uint64_t>> subscriber = topic->subscriber();
    ASSERT_TRUE(subscriber.has_value());
    subscribers.push_back(std::move(*subscriber));
  }

  std::atomic<bool> publisher_done{false};
  std::array<std::atomic<std::uint64_t>, kSubscriberCount> taken{};
  std::vector<std::unique_ptr<pal::IThread>> threads;

  for (std::uint32_t seat = 0; seat < kSubscriberCount; ++seat) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        platform.create_thread(thread_config("volt-topic-sub"), [&, seat] {
          Subscriber<std::uint64_t> &subscriber = subscribers[seat];
          while (true) {
            const core::expected<Sample<std::uint64_t>> sample = subscriber.take();
            if (sample.has_value()) {
              taken[seat].fetch_add(1, std::memory_order_relaxed);
              continue;
            }
            if (publisher_done.load(std::memory_order_acquire) && subscriber.pending() == 0) {
              return;
            }
          }
        });
    ASSERT_TRUE(thread.has_value());
    threads.push_back(std::move(*thread));
  }

  std::uint64_t published = 0;
  while (published < kMessages) {
    core::expected<Loan<std::uint64_t>> loan = publisher->loan();
    if (!loan.has_value()) {
      // The pool can empty while three consumers hold samples; backpressure
      // at the source is the design, so the producer simply retries.
      continue;
    }
    **loan = published;
    publisher->publish(std::move(*loan));
    published += 1;
  }
  publisher_done.store(true, std::memory_order_release);
  for (std::unique_ptr<pal::IThread> &thread : threads) {
    ASSERT_TRUE(thread->join().has_value());
  }

  // The books must balance per seat: everything published was either taken
  // or counted as dropped. Nothing vanishes silently (SPEC 42.1).
  for (std::uint32_t seat = 0; seat < kSubscriberCount; ++seat) {
    const std::uint64_t seen = taken[seat].load(std::memory_order_relaxed);
    const std::uint64_t lost = subscribers[seat].dropped();
    EXPECT_EQ(seen + lost, kMessages) << "seat " << seat;
  }
  subscribers.clear();
  EXPECT_EQ(topic->available_slots(), kConfig.slot_count);
  EXPECT_EQ(topic->lost_slots(), 0U) << "the pool gave up on a slot under contention";
}

} // namespace
} // namespace volt::ipc
