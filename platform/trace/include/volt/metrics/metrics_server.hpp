#pragma once

#include "volt/metrics/metric_registry.hpp"

#include "volt/core/error.hpp"
#include "volt/pal/platform.hpp"
#include "volt/pal/socket.hpp"
#include "volt/pal/stream_listener.hpp"
#include "volt/pal/thread.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace volt::metrics {

/// Largest exposition the server will produce.
///
/// A full registry of 256 metrics, the widest of them a summary of seven
/// lines, fits inside this with room to spare. The buffer is owned by the
/// server and reused for every scrape, so nothing is allocated while serving;
/// a page that would exceed it is sent truncated and the counter says so.
inline constexpr std::size_t kMaxExpositionBytes = std::size_t{64} * 1024;

/// Largest request the server will read.
///
/// A scrape sends a request line and a handful of headers, all of which this
/// holds several times over. Anything longer is answered and dropped rather
/// than buffered: the endpoint reports numbers, it does not accept uploads.
inline constexpr std::size_t kMaxRequestBytes = std::size_t{4} * 1024;

/// Largest set of response headers the server will produce.
///
/// The status line, a content type, a length and a connection header, none of
/// which vary in length by more than a few bytes. Kept separate from the body
/// so that a body which fills its buffer can still be sent under a length that
/// matches it.
inline constexpr std::size_t kMaxHeaderBytes = 512;

/// Serves the registry over HTTP at `/metrics`.
///
/// One connection at a time, served by one thread of its own: a scrape arrives
/// every few seconds and does no work worth parallelising, and a single thread
/// is one place where the control plane can be given its priority and its core
/// (AGENTS.md 6.4). Nothing here runs on the data plane.
///
/// The port is a parameter rather than a constant because SPEC 22.4 makes it
/// configurable; `config::TelemetryConfig::metrics_port` is where a node's
/// value comes from, passed in by whoever starts the server.
///
/// @thread start and stop from one thread; the serving thread is private
/// @rt     never call it from the control loop
class MetricsServer final {
public:
  /// Prepares a server that will report `registry`.
  ///
  /// @pre `platform` and `registry` both outlive this server
  MetricsServer(pal::IPlatform &platform, const MetricRegistry &registry) noexcept;

  /// Stops the server if it is still running.
  ~MetricsServer();

  // Rule of five because the serving thread holds a pointer to this object:
  // moving it would leave that thread reading a corpse.
  MetricsServer(const MetricsServer &) = delete;
  MetricsServer &operator=(const MetricsServer &) = delete;
  MetricsServer(MetricsServer &&) = delete;
  MetricsServer &operator=(MetricsServer &&) = delete;

  /// Binds `local` and starts serving.
  ///
  /// A zero port asks for an ephemeral one, which `endpoint()` then reports.
  ///
  /// @post   on success `/metrics` answers until `stop()`
  /// @errors kResourceBusy when the endpoint is taken, kResourceUnavailable
  ///         when the server is already running, and whatever the platform
  ///         reports for a listener or a thread it cannot create
  [[nodiscard]] core::expected<void> start(pal::Endpoint local) noexcept;

  /// Returns the endpoint being served.
  ///
  /// @errors kResourceUnavailable when the server is not running
  [[nodiscard]] core::expected<pal::Endpoint> endpoint() const noexcept;

  /// Stops serving and joins the serving thread.
  ///
  /// Safe to call on a server that was never started or is already stopped.
  ///
  /// @post   the port is released and the serving thread has ended
  [[nodiscard]] core::expected<void> stop() noexcept;

  /// Returns how many requests were answered with an exposition.
  [[nodiscard]] std::uint64_t scrapes_served() const noexcept;

  /// Returns how many requests were answered with an error status.
  [[nodiscard]] std::uint64_t requests_rejected() const noexcept;

  /// Returns how many expositions did not fit the buffer and were cut short.
  [[nodiscard]] std::uint64_t expositions_truncated() const noexcept;

private:
  /// Accepts connections until `stop()` asks it to finish.
  void serve() noexcept;

  /// Answers one connection and closes it.
  void answer(pal::IStreamSocket &connection) noexcept;

  /// Sends what it can of `payload` past `sent`, returning how much went.
  ///
  /// @errors kTransientPeerUnreachable when the peer has stopped reading
  [[nodiscard]] static core::expected<std::size_t>
  send_some(pal::IStreamSocket &connection, std::string_view payload, std::size_t sent) noexcept;

  /// Sends `payload` in full, or gives up when the peer stops reading.
  [[nodiscard]] static core::expected<void> send_all(pal::IStreamSocket &connection,
                                                     std::string_view payload) noexcept;

  /// Answers with `status` and no body, and counts the rejection.
  void reject(pal::IStreamSocket &connection, std::string_view status) noexcept;

  /// Collects the registry into the exposition buffer and returns it.
  [[nodiscard]] std::string_view render_exposition() noexcept;

  /// Writes the response headers for a body of `body_bytes`.
  ///
  /// An empty `content_type` leaves the header out, which is what a response
  /// with no body wants.
  [[nodiscard]] std::string_view render_headers(std::string_view status,
                                                std::string_view content_type,
                                                std::size_t body_bytes) noexcept;

  pal::IPlatform &platform_;
  const MetricRegistry &registry_;

  std::unique_ptr<pal::IStreamListener> listener_;
  std::unique_ptr<pal::IThread> thread_;

  // Read by the serving thread on every accept timeout and written by whoever
  // calls stop(). Release on the write pairs with acquire on the read so the
  // thread also sees everything stop() did before asking it to finish.
  std::atomic_bool stopping_{false};

  // Owned by the serving thread once it starts; nothing else touches them
  // while it runs, which is what keeps serving allocation-free.
  std::array<char, kMaxExpositionBytes> exposition_{};
  std::array<char, kMaxHeaderBytes> header_{};
  std::array<char, kMaxRequestBytes> request_{};

  std::atomic<std::uint64_t> scrapes_served_{0};
  std::atomic<std::uint64_t> requests_rejected_{0};
  std::atomic<std::uint64_t> expositions_truncated_{0};
};

} // namespace volt::metrics
