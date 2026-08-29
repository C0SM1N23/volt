#pragma once

#include "volt/log/format_entry.hpp"
#include "volt/log/level.hpp"
#include "volt/log/log_ring.hpp"
#include "volt/log/module.hpp"
#include "volt/log/record_writer.hpp"
#include "volt/pal/clock.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace volt::log {

/// Most threads that may log at once.
///
/// Every logging thread owns a ring, and the registry that the drain walks is
/// a fixed array rather than a growing container, so registration needs no
/// lock and the drain's work is bounded (SPEC 5.5). A deployment with more
/// threads than this has outgrown the assumption and should say so loudly
/// rather than quietly reallocate.
inline constexpr std::size_t kMaxLoggingThreads = 64;

/// The one logging front end.
///
/// A singleton, which AGENTS.md 2.9 permits for exactly this: a logger has to
/// be reachable from anywhere without being threaded through every signature,
/// and there is only ever one log.
class Logger final {
public:
  /// Returns the process-wide logger.
  [[nodiscard]] static Logger &instance() noexcept;

  /// Sets the lowest level `module` emits.
  ///
  /// @thread any; takes effect on other threads without further synchronisation
  void set_level(Module module, Level level) noexcept;

  /// Returns the lowest level `module` emits.
  [[nodiscard]] Level level_of(Module module) const noexcept;

  /// Sets the lowest level every module emits.
  void set_level_for_all(Level level) noexcept;

  /// Reports whether a record at this level and module would be kept.
  ///
  /// @rt one relaxed load; this is what a filtered-out call costs
  [[nodiscard]] bool enabled(Module module, Level level) const noexcept;

  /// Returns the calling thread's ring, creating it on first use.
  ///
  /// Creating it allocates, so a thread with a deadline calls this once during
  /// its setup rather than discovering the cost inside a control cycle.
  ///
  /// @post   returns nullptr once more than kMaxLoggingThreads have registered
  /// @thread any
  [[nodiscard]] static LogRing *ring_for_current_thread() noexcept;

  /// Registers this thread's ring ahead of its first log call.
  static void prepare_current_thread() noexcept;

  /// Returns the rings the drain has to walk.
  ///
  /// @thread the drain thread
  [[nodiscard]] std::span<LogRing *const> rings() noexcept;

  /// Returns how many records were thrown away for want of a ring.
  [[nodiscard]] std::uint64_t threads_refused() const noexcept;

  /// Creates a ring for the calling thread and keeps ownership of it.
  ///
  /// The logger owns every ring for the life of the process, deliberately: a
  /// ring owned by its producing thread would be freed when that thread exits,
  /// while the drain may still be reading records out of it. The cost is
  /// bounded at kMaxLoggingThreads rings and paid once per thread.
  ///
  /// @post returns nullptr when the registry is full
  [[nodiscard]] LogRing *create_ring() noexcept;

  /// Gives the logger the clock records are stamped with.
  ///
  /// Injected rather than read from the operating system here, because only
  /// `platform/pal` may talk to the system (AGENTS.md 2.15) and because a
  /// simulated run has to stamp its records with simulated time.
  ///
  /// @pre `clock` outlives every thread that logs
  void set_clock(pal::IClock &clock) noexcept;

  /// Returns the instant a record is stamped with, or zero before a clock has
  /// been installed.
  ///
  /// @rt one relaxed load and one clock read
  [[nodiscard]] std::int64_t timestamp_ns() const noexcept;

private:
  Logger() = default;

  // One level per module, read on every log call from every thread. Relaxed
  // throughout: a filter change is allowed to reach other threads a few
  // records late, and nothing else depends on when it lands.
  std::array<std::atomic<std::uint8_t>, kModuleCount> levels_{};

  std::array<std::unique_ptr<LogRing>, kMaxLoggingThreads> owned_rings_{};
  std::array<LogRing *, kMaxLoggingThreads> rings_{};

  // Published with release so a ring's storage is visible to the drain before
  // the count that exposes it.
  std::atomic<std::size_t> ring_count_{0};

  // Hands each new thread a slot of its own. Relaxed: it only reserves an
  // index, and the release on ring_count_ is what publishes the ring.
  std::atomic<std::size_t> next_ring_{0};

  std::atomic<std::uint64_t> threads_refused_{0};

  // Relaxed: installed once during startup, long before the threads that read
  // it exist, so there is nothing for it to be ordered against.
  std::atomic<pal::IClock *> clock_{nullptr};
};

/// Writes one argument of whatever type the caller passed.
///
/// A tiny overload set rather than a formatting library: the caller path only
/// ever stores bytes, and the shape of the value is decided while compiling.
template <typename T> void write_argument(RecordWriter &writer, const T &value) noexcept {
  if constexpr (std::same_as<T, bool>) {
    writer.add(static_cast<std::uint64_t>(value ? 1 : 0));
  } else if constexpr (std::floating_point<T>) {
    writer.add(static_cast<double>(value));
  } else if constexpr (std::unsigned_integral<T>) {
    writer.add(static_cast<std::uint64_t>(value));
  } else if constexpr (std::signed_integral<T>) {
    writer.add(static_cast<std::int64_t>(value));
  } else {
    writer.add(std::string_view{value});
  }
}

/// Encodes a record into the calling thread's ring.
///
/// @pre    the caller has already checked the filter
/// @rt     allocation-free and wait-free; drops rather than blocks
template <typename... Args>
void emit(std::uint64_t identifier, std::int64_t timestamp_ns, Level level, Module module,
          const Args &...arguments) noexcept {
  static_assert(sizeof...(Args) <= kMaxArguments,
                "a message with this many placeholders wants to be several messages");

  LogRing *const ring = Logger::ring_for_current_thread();
  if (ring == nullptr) {
    return;
  }
  const std::span<std::byte> slot = ring->claim();
  if (slot.empty()) {
    ring->drop();
    return;
  }

  RecordWriter writer{slot};
  writer.begin(identifier, timestamp_ns, level, module);
  (write_argument(writer, arguments), ...);

  const std::size_t used = writer.finish();
  if (used == 0) {
    ring->drop();
    return;
  }
  ring->publish(used);
}

} // namespace volt::log

/// Emits a record at `level` when the filter for `module` allows it.
///
/// The format string never reaches the record: it is registered once in the
/// collected section and the record carries its id, so the caller stores a few
/// integers and returns.
#define VOLT_LOG_EMIT(level, module, format_string, ...)                                           \
  do {                                                                                             \
    if (::volt::log::Logger::instance().enabled((module), (level))) {                              \
      VOLT_LOG_DEFINE_FORMAT(volt_log_format_entry, format_string);                                \
      ::volt::log::emit(volt_log_format_entry.id, ::volt::log::Logger::instance().timestamp_ns(),  \
                        (level), (module)__VA_OPT__(, ) __VA_ARGS__);                              \
    }                                                                                              \
  } while (false)

#define VOLT_LOG_TRACE(module, format_string, ...)                                                 \
  VOLT_LOG_EMIT(::volt::log::Level::kTrace, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
#define VOLT_LOG_DEBUG(module, format_string, ...)                                                 \
  VOLT_LOG_EMIT(::volt::log::Level::kDebug, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
#define VOLT_LOG_INFO(module, format_string, ...)                                                  \
  VOLT_LOG_EMIT(::volt::log::Level::kInfo, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
#define VOLT_LOG_WARN(module, format_string, ...)                                                  \
  VOLT_LOG_EMIT(::volt::log::Level::kWarn, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
#define VOLT_LOG_ERROR(module, format_string, ...)                                                 \
  VOLT_LOG_EMIT(::volt::log::Level::kError, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
#define VOLT_LOG_FATAL(module, format_string, ...)                                                 \
  VOLT_LOG_EMIT(::volt::log::Level::kFatal, (module), format_string __VA_OPT__(, ) __VA_ARGS__)
