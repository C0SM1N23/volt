#include "volt/memory/atomic_fixed_pool.hpp"
#include "volt/memory/bounded_queue.hpp"
#include "volt/memory/mpsc_bounded_queue.hpp"
#include "volt/memory/pool_index.hpp"
#include "volt/memory/seq_lock.hpp"

#include "volt/core/error_code.hpp"
#include "volt/core/types.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/thread.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace volt::memory {
namespace {

// Ten million successful transfers per queue are required by P08. Each
// transfer performs both an enqueue and a dequeue under TSan.
constexpr std::uint64_t kTransferCount = 10'000'000;
// Four producers exercise actual MPSC contention and divide ten million
// transfers evenly, avoiding a partial final producer range.
constexpr std::size_t kProducerCount = 4;
constexpr std::uint64_t kTransfersPerProducer = kTransferCount / kProducerCount;
// A capacity that fits in L1 while still forcing wraparound more than nine
// thousand times in the campaign. Changing it changes contention behavior.
constexpr std::size_t kQueueCapacity = 1024;
// Retries are bounded at one hundred per required transfer. The bound exists
// to turn a stalled endpoint into a deterministic failure rather than a hang.
constexpr std::uint64_t kRetryMultiplier = 100;
constexpr std::uint64_t kAttemptLimit = kTransferCount * kRetryMultiplier;
// One million publications are sufficient to force sustained overlap with
// each seqlock reader while keeping the dedicated TSan case below its timeout.
constexpr std::uint64_t kSeqLockPublicationCount = 1'000'000;
constexpr std::size_t kSeqLockReaderCount = 3;
// A million pool ownership transfers with four workers exercises tagged-head
// contention without duplicating the longer queue campaign.
constexpr std::uint64_t kPoolTransferCount = 1'000'000;
constexpr std::size_t kPoolWorkerCount = 4;
constexpr std::uint64_t kPoolTransfersPerWorker = kPoolTransferCount / kPoolWorkerCount;
constexpr std::size_t kPoolCapacity = 64;

// These load-test threads are normal-priority and share the runner's allowed
// CPU set; zero priority and mask are the explicit portable PAL values for
// that policy. The default stack is sufficient because workers hold no large
// local objects.
constexpr pal::SchedulingPolicy kTestPolicy = pal::SchedulingPolicy::kOther;
constexpr core::Priority kTestPriority{};
constexpr pal::CpuMask kInheritedCpuMask = 0;
constexpr std::size_t kDefaultStackBytes = 0;

[[nodiscard]] constexpr pal::ThreadConfig thread_config(std::string_view name) noexcept {
  return pal::ThreadConfig{.name = name,
                           .policy = kTestPolicy,
                           .priority = kTestPriority,
                           .cpu_mask = kInheritedCpuMask,
                           .stack_bytes = kDefaultStackBytes};
}

TEST(BoundedQueueConcurrencyTest, TransfersTenMillionValuesWithoutRaceOrLoss) {
  BoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::atomic_bool producer_ok{true};
  std::atomic_bool consumer_ok{true};
  pal::posix::PosixPlatform platform;

  core::expected<std::unique_ptr<pal::IThread>> consumer =
      platform.create_thread(thread_config("volt-spsc-cons"), [&queue, &consumer_ok] {
        std::uint64_t expected = 0;
        VOLT_LOOP_BOUND(kAttemptLimit);
        for (std::uint64_t attempt = 0; attempt < kAttemptLimit && expected < kTransferCount;
             ++attempt) {
          const core::expected<std::uint64_t> value = queue.try_pop();
          if (value.has_value()) {
            if (*value != expected) {
              consumer_ok.store(false, std::memory_order_relaxed);
              return;
            }
            ++expected;
          } else if (value.error() != core::ErrorCode::kResourceUnavailable) {
            consumer_ok.store(false, std::memory_order_relaxed);
            return;
          }
        }
        if (expected != kTransferCount) {
          consumer_ok.store(false, std::memory_order_relaxed);
        }
      });
  ASSERT_TRUE(consumer.has_value());

  core::expected<std::unique_ptr<pal::IThread>> producer =
      platform.create_thread(thread_config("volt-spsc-prod"), [&queue, &producer_ok] {
        std::uint64_t next = 0;
        VOLT_LOOP_BOUND(kAttemptLimit);
        for (std::uint64_t attempt = 0; attempt < kAttemptLimit && next < kTransferCount;
             ++attempt) {
          const core::expected<void> result = queue.try_push(next);
          if (result.has_value()) {
            ++next;
          } else if (result.error() != core::ErrorCode::kResourceExhausted) {
            producer_ok.store(false, std::memory_order_relaxed);
            return;
          }
        }
        if (next != kTransferCount) {
          producer_ok.store(false, std::memory_order_relaxed);
        }
      });
  ASSERT_TRUE(producer.has_value());

  ASSERT_TRUE((*producer)->join().has_value());
  ASSERT_TRUE((*consumer)->join().has_value());
  EXPECT_TRUE(producer_ok.load(std::memory_order_relaxed));
  EXPECT_TRUE(consumer_ok.load(std::memory_order_relaxed));
}

TEST(MpscBoundedQueueConcurrencyTest, TransfersTenMillionValuesWithoutRaceOrLoss) {
  MpscBoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::atomic_bool producers_ok{true};
  std::atomic_bool consumer_ok{true};
  pal::posix::PosixPlatform platform;

  core::expected<std::unique_ptr<pal::IThread>> consumer =
      platform.create_thread(thread_config("volt-mpsc-cons"), [&queue, &consumer_ok] {
        std::array<std::uint64_t, kProducerCount> expected{};
        std::uint64_t consumed = 0;
        VOLT_LOOP_BOUND(kAttemptLimit);
        for (std::uint64_t attempt = 0; attempt < kAttemptLimit && consumed < kTransferCount;
             ++attempt) {
          const core::expected<std::uint64_t> value = queue.try_pop();
          if (!value.has_value()) {
            continue;
          }
          const std::size_t producer = static_cast<std::size_t>(*value / kTransfersPerProducer);
          const std::uint64_t sequence = *value % kTransfersPerProducer;
          if (producer >= kProducerCount || sequence != expected[producer]) {
            consumer_ok.store(false, std::memory_order_relaxed);
            return;
          }
          ++expected[producer];
          ++consumed;
        }
        if (consumed != kTransferCount) {
          consumer_ok.store(false, std::memory_order_relaxed);
        }
      });
  ASSERT_TRUE(consumer.has_value());

  constexpr std::array<std::string_view, kProducerCount> kProducerNames{
      "volt-mpsc-p0", "volt-mpsc-p1", "volt-mpsc-p2", "volt-mpsc-p3"};
  std::array<std::unique_ptr<pal::IThread>, kProducerCount> producers{};
  for (std::size_t producer = 0; producer < kProducerCount; ++producer) {
    core::expected<std::unique_ptr<pal::IThread>> thread = platform.create_thread(
        thread_config(kProducerNames[producer]), [producer, &queue, &producers_ok] {
          std::uint64_t sequence = 0;
          VOLT_LOOP_BOUND(kAttemptLimit);
          for (std::uint64_t attempt = 0;
               attempt < kAttemptLimit && sequence < kTransfersPerProducer; ++attempt) {
            const std::uint64_t value =
                (static_cast<std::uint64_t>(producer) * kTransfersPerProducer) + sequence;
            const core::expected<void> result = queue.try_push(value);
            if (result.has_value()) {
              ++sequence;
            } else if (result.error() != core::ErrorCode::kResourceBusy &&
                       result.error() != core::ErrorCode::kResourceExhausted) {
              producers_ok.store(false, std::memory_order_relaxed);
              return;
            }
          }
          if (sequence != kTransfersPerProducer) {
            producers_ok.store(false, std::memory_order_relaxed);
          }
        });
    ASSERT_TRUE(thread.has_value());
    producers[producer] = std::move(*thread);
  }

  for (const std::unique_ptr<pal::IThread> &producer : producers) {
    ASSERT_TRUE(producer->join().has_value());
  }
  ASSERT_TRUE((*consumer)->join().has_value());
  EXPECT_TRUE(producers_ok.load(std::memory_order_relaxed));
  EXPECT_TRUE(consumer_ok.load(std::memory_order_relaxed));
}

struct VersionedSample final {
  std::uint64_t sequence;
  std::uint64_t complement;
};

TEST(SeqLockConcurrencyTest, MultipleReadersObserveOnlyCompletePublications) {
  SeqLock<VersionedSample> lock;
  lock.store(VersionedSample{.sequence = 0U, .complement = ~std::uint64_t{0U}});
  std::atomic_bool writer_done{false};
  std::atomic_bool readers_ok{true};
  pal::posix::PosixPlatform platform;

  constexpr std::array<std::string_view, kSeqLockReaderCount> kReaderNames{
      "volt-seqlock-r0", "volt-seqlock-r1", "volt-seqlock-r2"};
  std::array<std::unique_ptr<pal::IThread>, kSeqLockReaderCount> readers{};
  for (std::size_t reader = 0; reader < kSeqLockReaderCount; ++reader) {
    core::expected<std::unique_ptr<pal::IThread>> thread = platform.create_thread(
        thread_config(kReaderNames[reader]), [&lock, &writer_done, &readers_ok] {
          VOLT_LOOP_BOUND(kAttemptLimit);
          for (std::uint64_t attempt = 0; attempt < kAttemptLimit; ++attempt) {
            const core::expected<VersionedSample> sample = lock.load();
            if (sample.has_value() && sample->complement != ~sample->sequence) {
              readers_ok.store(false, std::memory_order_relaxed);
              return;
            }
            // Acquire observes the final publication sequenced before the
            // writer's release of this completion flag.
            const bool done = writer_done.load(std::memory_order_acquire);
            if (done && sample.has_value() && sample->sequence == kSeqLockPublicationCount) {
              return;
            }
          }
          readers_ok.store(false, std::memory_order_relaxed);
        });
    ASSERT_TRUE(thread.has_value());
    readers[reader] = std::move(*thread);
  }

  core::expected<std::unique_ptr<pal::IThread>> writer =
      platform.create_thread(thread_config("volt-seqlock-w"), [&lock, &writer_done] {
        VOLT_LOOP_BOUND(kSeqLockPublicationCount);
        for (std::uint64_t sequence = 1; sequence <= kSeqLockPublicationCount; ++sequence) {
          lock.store(VersionedSample{.sequence = sequence, .complement = ~sequence});
        }
        // Release makes the final seqlock publication visible before readers
        // accept the completion flag.
        writer_done.store(true, std::memory_order_release);
      });
  ASSERT_TRUE(writer.has_value());

  ASSERT_TRUE((*writer)->join().has_value());
  for (const std::unique_ptr<pal::IThread> &reader : readers) {
    ASSERT_TRUE(reader->join().has_value());
  }
  EXPECT_TRUE(readers_ok.load(std::memory_order_relaxed));
}

TEST(AtomicFixedPoolConcurrencyTest, NeverHandsOneSlotToTwoWorkers) {
  AtomicFixedPool<std::uint64_t, kPoolCapacity> pool;
  std::array<std::atomic_bool, kPoolCapacity> in_use{};
  std::atomic_bool workers_ok{true};
  pal::posix::PosixPlatform platform;
  constexpr std::array<std::string_view, kPoolWorkerCount> kWorkerNames{
      "volt-pool-w0", "volt-pool-w1", "volt-pool-w2", "volt-pool-w3"};
  std::array<std::unique_ptr<pal::IThread>, kPoolWorkerCount> workers{};

  for (std::size_t worker = 0; worker < kPoolWorkerCount; ++worker) {
    core::expected<std::unique_ptr<pal::IThread>> thread = platform.create_thread(
        thread_config(kWorkerNames[worker]), [worker, &pool, &in_use, &workers_ok] {
          std::uint64_t completed = 0;
          VOLT_LOOP_BOUND(kAttemptLimit);
          for (std::uint64_t attempt = 0;
               attempt < kAttemptLimit && completed < kPoolTransfersPerWorker; ++attempt) {
            const core::expected<PoolIndex> index = pool.allocate();
            if (!index.has_value()) {
              continue;
            }

            // Acquire/release pairs with the prior owner's release store, so a
            // true result can only mean the pool duplicated live ownership.
            const bool already_used =
                in_use[index->value()].exchange(true, std::memory_order_acq_rel);
            if (already_used) {
              workers_ok.store(false, std::memory_order_relaxed);
              return;
            }
            core::expected<std::reference_wrapper<std::uint64_t>> value = pool.get(*index);
            if (!value.has_value()) {
              workers_ok.store(false, std::memory_order_relaxed);
              return;
            }
            value->get() = static_cast<std::uint64_t>(worker);

            // Release makes the cleared ownership marker visible before the
            // pool publishes this slot to another worker.
            in_use[index->value()].store(false, std::memory_order_release);
            core::expected<void> released = pool.release(*index);
            VOLT_LOOP_BOUND(kAttemptLimit);
            for (std::uint64_t release_attempt = 0;
                 release_attempt < kAttemptLimit && !released.has_value(); ++release_attempt) {
              if (released.error() != core::ErrorCode::kResourceBusy) {
                workers_ok.store(false, std::memory_order_relaxed);
                return;
              }
              released = pool.release(*index);
            }
            if (!released.has_value()) {
              workers_ok.store(false, std::memory_order_relaxed);
              return;
            }
            ++completed;
          }
          if (completed != kPoolTransfersPerWorker) {
            workers_ok.store(false, std::memory_order_relaxed);
          }
        });
    ASSERT_TRUE(thread.has_value());
    workers[worker] = std::move(*thread);
  }

  for (const std::unique_ptr<pal::IThread> &worker : workers) {
    ASSERT_TRUE(worker->join().has_value());
  }
  EXPECT_TRUE(workers_ok.load(std::memory_order_relaxed));
  EXPECT_EQ(pool.available(), kPoolCapacity);
}

} // namespace
} // namespace volt::memory
