#include "volt/log/log_drain.hpp"
#include "volt/log/logger.hpp"
#include "volt/log/record_reader.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace volt::log {
namespace {

// Eight producers is the thread budget of SPEC 42.2 with room to spare, and a
// million records each is far more than any ring can hold, so the run really
// exercises a drain that has to keep up rather than one that never fills.
constexpr int kProducerCount = 8;
constexpr int kRecordsPerProducer = 1'000'000;

/// Counts what it is given without keeping it, so the run measures the queue
/// rather than the memory it would take to store eight million records.
class CountingSink final : public ILogSink {
public:
  [[nodiscard]] core::expected<void> write(std::span<const std::byte> record) noexcept override {
    RecordReader reader{record};
    if (!reader.parse_header().has_value()) {
      malformed_.fetch_add(1, std::memory_order_relaxed);
      return {};
    }
    written_.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  [[nodiscard]] core::expected<void> flush() noexcept override { return {}; }

  [[nodiscard]] std::uint64_t written() const noexcept {
    return written_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t malformed() const noexcept {
    return malformed_.load(std::memory_order_relaxed);
  }

private:
  // Only the drain thread writes these, but a test thread reads them, so they
  // are atomic; relaxed because nothing else is ordered against the counts.
  std::atomic<std::uint64_t> written_{0};
  std::atomic<std::uint64_t> malformed_{0};
};

/// Starts the producers and returns their handles.
[[nodiscard]] std::vector<std::unique_ptr<pal::IThread>> start_producers(pal::IPlatform &platform) {
  std::vector<std::unique_ptr<pal::IThread>> producers;
  for (int index = 0; index < kProducerCount; ++index) {
    core::expected<std::unique_ptr<pal::IThread>> thread =
        platform.create_thread(pal::ThreadConfig{.name = "volt-log-load"}, [index] {
          // Registering the ring here rather than on the first log call keeps
          // the allocation out of the measured loop, which is what a
          // real-time thread has to do during its setup.
          Logger::prepare_current_thread();
          for (int record = 0; record < kRecordsPerProducer; ++record) {
            VOLT_LOG_INFO(Module::kCore, "producer {} record {}", index, record);
          }
        });
    EXPECT_TRUE(thread.has_value());
    producers.push_back(std::move(*thread));
  }
  return producers;
}

void join_all(const std::vector<std::unique_ptr<pal::IThread>> &producers) {
  for (const std::unique_ptr<pal::IThread> &producer : producers) {
    EXPECT_TRUE(producer->join().has_value());
  }
}

TEST(LogConcurrencyTest, NothingIsLostWithoutBeingCounted) {
  // The property is conservation, not delivery: under load a producer is
  // allowed to drop rather than stall a control cycle, but every dropped
  // record has to show up in a counter. A log that quietly loses messages is
  // worse than no log, because it makes the gaps invisible.
  pal::posix::PosixPlatform platform;
  Logger::instance().set_clock(platform.clock());
  Logger::instance().set_level_for_all(Level::kTrace);

  CountingSink sink;
  LogDrain drain{Logger::instance(), sink, platform};
  ASSERT_TRUE(drain.start().has_value());

  join_all(start_producers(platform));
  drain.stop();

  constexpr std::uint64_t kProduced =
      static_cast<std::uint64_t>(kProducerCount) * kRecordsPerProducer;
  const std::uint64_t accounted = sink.written() + drain.records_dropped_by_producers();

  EXPECT_EQ(sink.malformed(), 0U);
  EXPECT_EQ(accounted, kProduced) << "written=" << sink.written()
                                  << " dropped=" << drain.records_dropped_by_producers();

  // The drain has to be doing real work. Without this the test would still
  // pass if every record were dropped, and it would then be measuring the
  // drop path rather than the queue.
  EXPECT_GT(sink.written(), kProduced / 100)
      << "the drain wrote almost nothing: written=" << sink.written();
}

} // namespace
} // namespace volt::log
