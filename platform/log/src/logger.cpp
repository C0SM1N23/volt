#include "volt/log/logger.hpp"

namespace volt::log {
Logger &Logger::instance() noexcept {
  // Function-local static: constructed on first use and destroyed after main,
  // which is later than any thread that might still be logging.
  static Logger logger;
  return logger;
}

void Logger::set_level(Module module, Level level) noexcept {
  const auto index = static_cast<std::size_t>(module);
  if (index >= kModuleCount) {
    return;
  }
  // Relaxed: a filter change may reach other threads a few records late, and
  // nothing else is ordered against it.
  levels_[index].store(static_cast<std::uint8_t>(level), std::memory_order_relaxed);
}

Level Logger::level_of(Module module) const noexcept {
  const auto index = static_cast<std::size_t>(module);
  if (index >= kModuleCount) {
    return kSilent;
  }
  return static_cast<Level>(levels_[index].load(std::memory_order_relaxed));
}

void Logger::set_level_for_all(Level level) noexcept {
  for (std::atomic<std::uint8_t> &entry : levels_) {
    entry.store(static_cast<std::uint8_t>(level), std::memory_order_relaxed);
  }
}

bool Logger::enabled(Module module, Level level) const noexcept {
  return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(level_of(module));
}

LogRing *Logger::create_ring() noexcept {
  // fetch_add rather than a load and a store: several threads may reach their
  // first log call at once, and each has to get a slot of its own.
  const std::size_t index = next_ring_.fetch_add(1, std::memory_order_relaxed);
  if (index >= kMaxLoggingThreads) {
    threads_refused_.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
  }

  owned_rings_[index] = std::make_unique<LogRing>();
  rings_[index] = owned_rings_[index].get();
  // Release: the ring's storage must be visible to the drain before the count
  // that exposes it. The count only ever grows to cover contiguous slots, so a
  // thread that claimed a later index waits for the earlier ones to publish.
  std::size_t expected = index;
  while (!ring_count_.compare_exchange_weak(expected, index + 1, std::memory_order_release,
                                            std::memory_order_relaxed)) {
    expected = index;
  }
  return rings_[index];
}

LogRing *Logger::ring_for_current_thread() noexcept {
  thread_local LogRing *const ring = Logger::instance().create_ring();
  return ring;
}

void Logger::prepare_current_thread() noexcept { static_cast<void>(ring_for_current_thread()); }

std::span<LogRing *const> Logger::rings() noexcept {
  // Acquire: pairs with the release in register_ring, so every ring counted
  // here is fully constructed.
  const std::size_t count = ring_count_.load(std::memory_order_acquire);
  return std::span<LogRing *const>{rings_.data(), count};
}

std::uint64_t Logger::threads_refused() const noexcept {
  return threads_refused_.load(std::memory_order_relaxed);
}

void Logger::set_clock(pal::IClock &clock) noexcept {
  clock_.store(&clock, std::memory_order_relaxed);
}

std::int64_t Logger::timestamp_ns() const noexcept {
  const pal::IClock *const clock = clock_.load(std::memory_order_relaxed);
  if (clock == nullptr) {
    // Records produced before startup installed a clock still carry their
    // order, which is what the drain writes them in; only the instant is
    // unknown, and an obviously impossible zero says so.
    return 0;
  }
  return clock->monotonic().ns_since_epoch();
}

} // namespace volt::log
