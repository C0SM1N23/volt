#include "volt/memory/bounded_queue.hpp"
#include "volt/memory/mpsc_bounded_queue.hpp"

#include "volt/core/error.hpp"
#include "volt/core/types.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/thread.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace volt::memory {
namespace {

// A thousand and twenty-four slots match the concurrency campaign and fit in
// L1 for the measured 64-bit payload. Changing capacity changes cache misses,
// so results with another value are not directly comparable.
constexpr std::size_t kQueueCapacity = 1024;
// Ten million end-to-end transfers make throughput insensitive to thread
// startup and match the P08 stress-test scale.
constexpr std::int64_t kConcurrentTransfers = 10'000'000;
constexpr std::size_t kConcurrentProducerCount = 4;
constexpr std::uint64_t kTransfersPerProducer =
    static_cast<std::uint64_t>(kConcurrentTransfers) / kConcurrentProducerCount;
// A million bounded retries tolerates ordinary scheduler preemption while
// still turning a stalled endpoint into a benchmark error instead of a hang.
constexpr std::size_t kRetriesPerOperation = 1'000'000;
// Ten thousand batches provide stable P50/P99 ranks. A thousand operations
// per batch amortize the two vDSO clock reads over the queue path.
constexpr std::size_t kLatencyBatches = 10'000;
constexpr std::size_t kOperationsPerBatch = 1'000;
constexpr std::size_t kWarmupOperations = 10'000;
constexpr std::size_t kPercentScale = 1'000;
constexpr std::size_t kP50PerThousand = 500;
constexpr std::size_t kP99PerThousand = 990;

// Benchmark workers use the portable non-real-time PAL configuration because
// this development machine is not the isolated PREEMPT_RT reference target.
constexpr pal::SchedulingPolicy kBenchmarkPolicy = pal::SchedulingPolicy::kOther;
constexpr core::Priority kBenchmarkPriority{};
constexpr pal::CpuMask kInheritedCpuMask = 0;
constexpr std::size_t kDefaultStackBytes = 0;

[[nodiscard]] constexpr pal::ThreadConfig thread_config(std::string_view name) noexcept {
  return pal::ThreadConfig{.name = name,
                           .policy = kBenchmarkPolicy,
                           .priority = kBenchmarkPriority,
                           .cpu_mask = kInheritedCpuMask,
                           .stack_bytes = kDefaultStackBytes};
}

template <typename Queue>
[[nodiscard]] core::expected<void> push_bounded(Queue &queue, std::uint64_t value) noexcept {
  VOLT_LOOP_BOUND(kRetriesPerOperation);
  for (std::size_t attempt = 0; attempt < kRetriesPerOperation; ++attempt) {
    const core::expected<void> pushed = queue.try_push(value);
    if (pushed.has_value()) {
      return {};
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

template <typename Queue>
[[nodiscard]] core::expected<std::uint64_t> pop_bounded(Queue &queue) noexcept {
  VOLT_LOOP_BOUND(kRetriesPerOperation);
  for (std::size_t attempt = 0; attempt < kRetriesPerOperation; ++attempt) {
    const core::expected<std::uint64_t> value = queue.try_pop();
    if (value.has_value()) {
      return value;
    }
  }
  return std::unexpected{core::ErrorCode::kResourceBusy};
}

[[nodiscard]] double percentile(const std::vector<double> &sorted, std::size_t per_thousand) {
  const std::size_t index = (sorted.size() * per_thousand) / kPercentScale;
  return sorted[std::min(index, sorted.size() - 1U)];
}

void benchmark_spsc_round_trip(benchmark::State &state) {
  BoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::uint64_t value = 0;
  for ([[maybe_unused]] const auto iteration : state) {
    const core::expected<void> pushed = queue.try_push(value);
    const core::expected<std::uint64_t> popped = queue.try_pop();
    if (!pushed.has_value() || !popped.has_value()) {
      state.SkipWithError("SPSC round trip unexpectedly failed");
      return;
    }
    std::uint64_t observed = *popped;
    benchmark::DoNotOptimize(observed);
    ++value;
  }
  state.SetItemsProcessed(state.iterations());
}

void benchmark_mpsc_single_producer_round_trip(benchmark::State &state) {
  MpscBoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::uint64_t value = 0;
  for ([[maybe_unused]] const auto iteration : state) {
    const core::expected<void> pushed = queue.try_push(value);
    const core::expected<std::uint64_t> popped = queue.try_pop();
    if (!pushed.has_value() || !popped.has_value()) {
      state.SkipWithError("MPSC round trip unexpectedly failed");
      return;
    }
    std::uint64_t observed = *popped;
    benchmark::DoNotOptimize(observed);
    ++value;
  }
  state.SetItemsProcessed(state.iterations());
}

template <typename Queue> void measure_latency_distribution(benchmark::State &state) {
  Queue queue;
  pal::posix::PosixPlatform platform;
  std::vector<double> per_operation_ns;
  per_operation_ns.reserve(kLatencyBatches);
  std::uint64_t value = 0;

  VOLT_LOOP_BOUND(kWarmupOperations);
  for (std::size_t operation = 0; operation < kWarmupOperations; ++operation) {
    const core::expected<void> pushed = queue.try_push(value);
    const core::expected<std::uint64_t> popped = queue.try_pop();
    if (!pushed.has_value() || !popped.has_value()) {
      state.SkipWithError("queue warmup unexpectedly failed");
      return;
    }
    std::uint64_t observed = *popped;
    benchmark::DoNotOptimize(observed);
    ++value;
  }

  for ([[maybe_unused]] const auto iteration : state) {
    VOLT_LOOP_BOUND(kLatencyBatches);
    for (std::size_t batch = 0; batch < kLatencyBatches; ++batch) {
      const core::Timestamp start = platform.clock().monotonic();
      VOLT_LOOP_BOUND(kOperationsPerBatch);
      for (std::size_t operation = 0; operation < kOperationsPerBatch; ++operation) {
        const core::expected<void> pushed = queue.try_push(value);
        const core::expected<std::uint64_t> popped = queue.try_pop();
        if (!pushed.has_value() || !popped.has_value()) {
          state.SkipWithError("queue latency batch unexpectedly failed");
          return;
        }
        std::uint64_t observed = *popped;
        benchmark::DoNotOptimize(observed);
        ++value;
      }
      const core::Timestamp finish = platform.clock().monotonic();
      const core::expected<core::Duration> elapsed = finish.checked_since(start);
      if (!elapsed.has_value()) {
        state.SkipWithError("monotonic clock moved outside its representation");
        return;
      }
      per_operation_ns.push_back(static_cast<double>(elapsed->ns()) /
                                 static_cast<double>(kOperationsPerBatch));
    }
  }

  std::ranges::sort(per_operation_ns);
  state.counters["p50_ns"] = percentile(per_operation_ns, kP50PerThousand);
  state.counters["p99_ns"] = percentile(per_operation_ns, kP99PerThousand);
  state.SetItemsProcessed(static_cast<std::int64_t>(kLatencyBatches * kOperationsPerBatch));
}

void benchmark_spsc_concurrent_throughput(benchmark::State &state) {
  BoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::atomic_bool producer_ok{true};
  pal::posix::PosixPlatform platform;
  core::expected<std::unique_ptr<pal::IThread>> producer =
      platform.create_thread(thread_config("volt-bench-spsc"), [&queue, &producer_ok] {
        VOLT_LOOP_BOUND(kConcurrentTransfers);
        for (std::uint64_t value = 0; value < static_cast<std::uint64_t>(kConcurrentTransfers);
             ++value) {
          if (!push_bounded(queue, value).has_value()) {
            producer_ok.store(false, std::memory_order_relaxed);
            return;
          }
        }
      });
  if (!producer.has_value()) {
    state.SkipWithError("PAL could not start the SPSC producer");
    return;
  }

  for ([[maybe_unused]] const auto iteration : state) {
    const core::expected<std::uint64_t> value = pop_bounded(queue);
    if (!value.has_value()) {
      state.SkipWithError("SPSC consumer exceeded its retry bound");
      break;
    }
    std::uint64_t observed = *value;
    benchmark::DoNotOptimize(observed);
  }
  const core::expected<void> joined = (*producer)->join();
  if (!joined.has_value() || !producer_ok.load(std::memory_order_relaxed)) {
    state.SkipWithError("SPSC producer did not complete");
  }
  state.SetItemsProcessed(state.iterations());
}

void benchmark_mpsc_concurrent_throughput(benchmark::State &state) {
  MpscBoundedQueue<std::uint64_t, kQueueCapacity> queue;
  std::atomic_bool producers_ok{true};
  pal::posix::PosixPlatform platform;
  constexpr std::array<std::string_view, kConcurrentProducerCount> kProducerNames{
      "volt-bench-mp0", "volt-bench-mp1", "volt-bench-mp2", "volt-bench-mp3"};
  std::array<std::unique_ptr<pal::IThread>, kConcurrentProducerCount> producers{};

  for (std::size_t producer = 0; producer < kConcurrentProducerCount; ++producer) {
    core::expected<std::unique_ptr<pal::IThread>> thread = platform.create_thread(
        thread_config(kProducerNames[producer]), [producer, &queue, &producers_ok] {
          VOLT_LOOP_BOUND(kTransfersPerProducer);
          for (std::uint64_t sequence = 0; sequence < kTransfersPerProducer; ++sequence) {
            const std::uint64_t value =
                (static_cast<std::uint64_t>(producer) * kTransfersPerProducer) + sequence;
            if (!push_bounded(queue, value).has_value()) {
              producers_ok.store(false, std::memory_order_relaxed);
              return;
            }
          }
        });
    if (!thread.has_value()) {
      state.SkipWithError("PAL could not start an MPSC producer");
      return;
    }
    producers[producer] = std::move(*thread);
  }

  for ([[maybe_unused]] const auto iteration : state) {
    const core::expected<std::uint64_t> value = pop_bounded(queue);
    if (!value.has_value()) {
      state.SkipWithError("MPSC consumer exceeded its retry bound");
      break;
    }
    std::uint64_t observed = *value;
    benchmark::DoNotOptimize(observed);
  }
  for (const std::unique_ptr<pal::IThread> &producer : producers) {
    const core::expected<void> joined = producer->join();
    if (!joined.has_value()) {
      state.SkipWithError("an MPSC producer could not be joined");
      return;
    }
  }
  if (!producers_ok.load(std::memory_order_relaxed)) {
    state.SkipWithError("an MPSC producer exceeded its retry bound");
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(benchmark_spsc_round_trip)->Unit(benchmark::kNanosecond);
BENCHMARK(benchmark_mpsc_single_producer_round_trip)->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(measure_latency_distribution, BoundedQueue<std::uint64_t, kQueueCapacity>)
    ->Iterations(1);
BENCHMARK_TEMPLATE(measure_latency_distribution, MpscBoundedQueue<std::uint64_t, kQueueCapacity>)
    ->Iterations(1);
BENCHMARK(benchmark_spsc_concurrent_throughput)
    ->Iterations(kConcurrentTransfers)
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);
BENCHMARK(benchmark_mpsc_concurrent_throughput)
    ->Iterations(kConcurrentTransfers)
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);

} // namespace
} // namespace volt::memory

BENCHMARK_MAIN();
