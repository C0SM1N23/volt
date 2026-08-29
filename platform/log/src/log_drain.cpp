#include "volt/log/log_drain.hpp"

#include <utility>

namespace volt::log {
namespace {

// The io-offload thread runs below everything with a deadline, so a disk that
// is slow to answer costs throughput and never a control cycle (SPEC 42.2).
constexpr pal::ThreadConfig kDrainThreadConfig{.name = "volt-io-offload",
                                               .policy = pal::SchedulingPolicy::kOther,
                                               .priority = core::Priority{},
                                               .cpu_mask = 0,
                                               .stack_bytes = 0};

} // namespace

LogDrain::~LogDrain() { stop(); }

std::size_t LogDrain::drain_ring(LogRing &ring) noexcept {
  std::size_t written = 0;
  while (true) {
    const std::span<const std::byte> record = ring.pop();
    if (record.empty()) {
      return written;
    }
    if (sink_->write(record).has_value()) {
      written += 1;
      continue;
    }
    // A sink that refuses is not a reason to stop draining: the ring still has
    // to be emptied so its producer keeps making progress, and the refusal is
    // counted so the gap in the log is visible.
    refused_.fetch_add(1, std::memory_order_relaxed);
  }
}

std::size_t LogDrain::drain_once() noexcept {
  std::size_t written = 0;
  for (LogRing *const ring : logger_->rings()) {
    if (ring != nullptr) {
      written += drain_ring(*ring);
    }
  }
  return written;
}

void LogDrain::run() noexcept {
  while (running_.load(std::memory_order_acquire)) {
    const std::size_t written = drain_once();
    if (written == 0) {
      // Nothing was waiting, so the thread stands down rather than spinning
      // through every ring in a tight loop.
      const core::expected<void> slept = platform_->clock().sleep_for(kDrainIdleInterval);
      static_cast<void>(slept.has_value());
    }
  }
  // One last pass, so records published just before the stop request are not
  // left behind in a ring.
  const std::size_t trailing = drain_once();
  static_cast<void>(trailing);
  const core::expected<void> flushed = sink_->flush();
  static_cast<void>(flushed.has_value());
}

core::expected<void> LogDrain::start() noexcept {
  if (running_.load(std::memory_order_acquire)) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  running_.store(true, std::memory_order_release);

  core::expected<std::unique_ptr<pal::IThread>> thread =
      platform_->create_thread(kDrainThreadConfig, [this] { run(); });
  if (!thread.has_value()) {
    running_.store(false, std::memory_order_release);
    return std::unexpected{thread.error()};
  }
  thread_ = std::move(*thread);
  return {};
}

void LogDrain::stop() noexcept {
  if (thread_ == nullptr) {
    return;
  }
  running_.store(false, std::memory_order_release);
  const core::expected<void> joined = thread_->join();
  // A drain that will not join has already stopped producing records, and the
  // process is on its way down; there is nobody left to report it to.
  static_cast<void>(joined.has_value());
  thread_.reset();
}

std::uint64_t LogDrain::records_refused_by_sink() const noexcept {
  return refused_.load(std::memory_order_relaxed);
}

std::uint64_t LogDrain::records_dropped_by_producers() const noexcept {
  std::uint64_t total = 0;
  for (LogRing *const ring : logger_->rings()) {
    if (ring != nullptr) {
      total += ring->dropped();
    }
  }
  return total;
}

} // namespace volt::log
