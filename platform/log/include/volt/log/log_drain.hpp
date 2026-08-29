#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"
#include "volt/log/log_sink.hpp"
#include "volt/log/logger.hpp"
#include "volt/pal/platform.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace volt::log {

/// How long the drain waits when every ring was empty.
///
/// Long enough that an idle system is not woken constantly, short enough that
/// a burst is picked up before a ring fills. The rings hold roughly a thousand
/// records each, so this only matters when a thread logs faster than that per
/// interval, which is itself a sign of over-logging.
inline constexpr core::Duration kDrainIdleInterval = core::Duration::from_ms(5);

/// Moves records from every producer's ring into a sink.
///
/// Runs on the io-offload thread of SPEC 42.2, at a nice-ed priority, because
/// writing to disk blocks and nothing on the control path may wait for it. The
/// producers never wait for this thread: if it falls behind, records are
/// dropped and counted rather than allowed to stall a control cycle.
class LogDrain final {
public:
  /// @pre `logger`, `sink` and `platform` outlive this drain
  LogDrain(Logger &logger, ILogSink &sink, pal::IPlatform &platform) noexcept
      : logger_{&logger}, sink_{&sink}, platform_{&platform} {}

  /// Stops the thread if `start` was called.
  ~LogDrain();

  LogDrain(const LogDrain &) = delete;
  LogDrain &operator=(const LogDrain &) = delete;
  LogDrain(LogDrain &&) = delete;
  LogDrain &operator=(LogDrain &&) = delete;

  /// Starts the io-offload thread.
  ///
  /// @post   on success the drain runs until `stop`
  /// @errors whatever the platform reports when a thread cannot be created
  [[nodiscard]] core::expected<void> start() noexcept;

  /// Asks the thread to finish and waits for it.
  ///
  /// @post every record published before this call has reached the sink
  void stop() noexcept;

  /// Moves whatever is waiting into the sink and returns how many records
  /// were written.
  ///
  /// Exposed so a test can drain without a thread, which is what makes the
  /// round-trip and rotation tests deterministic.
  ///
  /// @thread the drain thread, or any single thread when no drain is running
  [[nodiscard]] std::size_t drain_once() noexcept;

  /// Returns how many records the sink refused.
  [[nodiscard]] std::uint64_t records_refused_by_sink() const noexcept;

  /// Returns how many records producers threw away because their ring was full.
  ///
  /// Summed across every ring, so nothing is lost without being counted.
  [[nodiscard]] std::uint64_t records_dropped_by_producers() const noexcept;

private:
  /// The body of the io-offload thread.
  void run() noexcept;

  /// Empties one ring into the sink and returns how many records it wrote.
  [[nodiscard]] std::size_t drain_ring(LogRing &ring) noexcept;

  Logger *logger_;
  ILogSink *sink_;
  pal::IPlatform *platform_;
  std::unique_ptr<pal::IThread> thread_;

  // Read by the drain thread, written by whoever stops it. Release/acquire so
  // the thread sees everything the stopper did before asking it to finish.
  std::atomic<bool> running_{false};

  std::atomic<std::uint64_t> refused_{0};
};

} // namespace volt::log
