#include "volt/metrics/metrics_server.hpp"

#include "volt/metrics/exposition_writer.hpp"
#include "volt/metrics/prometheus_exporter.hpp"

#include "volt/core/duration.hpp"
#include "volt/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {
namespace {

// How long an accept waits before the serving thread looks at the stop flag.
// It is also the longest a stop() can take to be noticed, so it is short
// enough not to delay a shutdown and long enough not to spin a core.
constexpr core::Duration kAcceptPollInterval = core::Duration::from_ms(50);

// How long a request may take to arrive once a peer has connected. A scrape
// sends its request immediately; a connection that opens and says nothing is a
// port scan, and holding the single serving thread for it would be a denial of
// service against every later scrape.
constexpr core::Duration kRequestTimeout = core::Duration::from_ms(500);

// The path Prometheus scrapes, fixed by SPEC 22.4.
constexpr std::string_view kMetricsPath = "/metrics";

// The serving thread runs at the default policy and inherits the process CPU
// set, stated here rather than left to the platform because AGENTS.md 6.4 asks
// every thread to say what it asked for. A scrape must never preempt the
// control loop, so it deliberately takes no real-time priority; zero priority
// and zero mask are the portable PAL values for that policy, and the default
// stack is enough for a thread that holds one request buffer of its own.
constexpr pal::SchedulingPolicy kServingPolicy = pal::SchedulingPolicy::kOther;
constexpr core::Priority kServingPriority{};
constexpr pal::CpuMask kInheritedCpuMask = 0;
constexpr std::size_t kDefaultStackBytes = 0;
constexpr std::string_view kServingThreadName = "volt-metrics";

constexpr std::string_view kGetMethod = "GET ";
constexpr std::string_view kRequestLineEnd = "\r\n";

constexpr std::string_view kStatusOk = "200 OK";
constexpr std::string_view kStatusNotFound = "404 Not Found";
constexpr std::string_view kStatusMethodNotAllowed = "405 Method Not Allowed";
constexpr std::string_view kStatusRequestTooLarge = "431 Request Header Fields Too Large";

// The content type the Prometheus text exposition format is served under.
constexpr std::string_view kExpositionContentType = "text/plain; version=0.0.4; charset=utf-8";

/// Returns the path from a request line, or nothing when it is not a GET.
[[nodiscard]] std::string_view path_of(std::string_view request) noexcept {
  const std::size_t line_end = request.find(kRequestLineEnd);
  if (line_end == std::string_view::npos) {
    return {};
  }

  const std::string_view line = request.substr(0, line_end);
  if (!line.starts_with(kGetMethod)) {
    return {};
  }

  const std::string_view after_method = line.substr(kGetMethod.size());
  const std::size_t path_end = after_method.find(' ');
  // A request line with no version after the path is malformed, and treating
  // the rest of the line as a path would answer a request nobody made.
  if (path_end == std::string_view::npos) {
    return {};
  }
  return after_method.substr(0, path_end);
}

} // namespace

MetricsServer::MetricsServer(pal::IPlatform &platform, const MetricRegistry &registry) noexcept
    : platform_{platform}, registry_{registry} {}

MetricsServer::~MetricsServer() {
  // A destructor cannot report anything, and a join that fails here would fail
  // for a thread that is already gone.
  const core::expected<void> stopped = stop();
  static_cast<void>(stopped.has_value());
}

core::expected<void> MetricsServer::start(pal::Endpoint local) noexcept {
  if (thread_ != nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  // Checked by hand rather than with VOLT_TRY: the result owns a listener and
  // cannot be copied, which is what that macro does to inspect it.
  core::expected<std::unique_ptr<pal::IStreamListener>> listener =
      platform_.listen_stream(local, pal::kDefaultListenBacklog);
  if (!listener.has_value()) {
    return std::unexpected{listener.error()};
  }
  VOLT_TRY((*listener)->set_accept_timeout(kAcceptPollInterval));

  listener_ = std::move(*listener);
  stopping_.store(false, std::memory_order_relaxed);

  core::expected<std::unique_ptr<pal::IThread>> thread =
      platform_.create_thread(pal::ThreadConfig{.name = kServingThreadName,
                                                .policy = kServingPolicy,
                                                .priority = kServingPriority,
                                                .cpu_mask = kInheritedCpuMask,
                                                .stack_bytes = kDefaultStackBytes},
                              [this] { serve(); });
  if (!thread.has_value()) {
    listener_.reset();
    return std::unexpected{thread.error()};
  }

  thread_ = std::move(*thread);
  return {};
}

core::expected<pal::Endpoint> MetricsServer::endpoint() const noexcept {
  if (listener_ == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return listener_->local_endpoint();
}

core::expected<void> MetricsServer::stop() noexcept {
  if (thread_ == nullptr) {
    return {};
  }

  // Release so the serving thread, which reads this with acquire, also sees
  // everything the stopping thread did before deciding to stop.
  stopping_.store(true, std::memory_order_release);
  const core::expected<void> joined = thread_->join();
  thread_.reset();
  listener_.reset();
  return joined;
}

void MetricsServer::serve() noexcept {
  while (!stopping_.load(std::memory_order_acquire)) {
    core::expected<std::unique_ptr<pal::IStreamSocket>> connection = listener_->accept();
    if (!connection.has_value()) {
      // A timeout is how the loop gets to look at the stop flag; any other
      // failure is the listener's, and retrying is what keeps one bad
      // connection from taking the endpoint down.
      continue;
    }
    answer(**connection);
  }
}

void MetricsServer::answer(pal::IStreamSocket &connection) noexcept {
  if (!connection.set_receive_timeout(kRequestTimeout).has_value()) {
    requests_rejected_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  const std::span<std::byte> buffer{reinterpret_cast<std::byte *>(request_.data()),
                                    request_.size()};
  const core::expected<std::size_t> received = connection.receive(buffer);
  if (!received.has_value() || *received == 0) {
    requests_rejected_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  const std::string_view request{request_.data(), *received};
  const std::string_view path = path_of(request);
  if (path.empty()) {
    const std::string_view status =
        *received == request_.size() ? kStatusRequestTooLarge : kStatusMethodNotAllowed;
    reject(connection, status);
    return;
  }
  if (path != kMetricsPath) {
    reject(connection, kStatusNotFound);
    return;
  }

  const std::string_view body = render_exposition();
  const std::string_view headers = render_headers(kStatusOk, kExpositionContentType, body.size());
  if (send_all(connection, headers).has_value() && send_all(connection, body).has_value()) {
    scrapes_served_.fetch_add(1, std::memory_order_relaxed);
  }
}

void MetricsServer::reject(pal::IStreamSocket &connection, std::string_view status) noexcept {
  requests_rejected_.fetch_add(1, std::memory_order_relaxed);
  // A peer that asked for the wrong thing and then stopped reading has already
  // been counted; there is nothing further to report about it.
  const core::expected<void> sent = send_all(connection, render_headers(status, {}, 0));
  static_cast<void>(sent.has_value());
}

std::string_view MetricsServer::render_exposition() noexcept {
  PrometheusExporter exporter{exposition_};
  registry_.collect(exporter);
  if (exporter.truncated()) {
    expositions_truncated_.fetch_add(1, std::memory_order_relaxed);
  }
  return exporter.view();
}

std::string_view MetricsServer::render_headers(std::string_view status,
                                               std::string_view content_type,
                                               std::size_t body_bytes) noexcept {
  ExpositionWriter headers{header_};
  headers.append("HTTP/1.1 ");
  headers.append(status);
  if (!content_type.empty()) {
    headers.append("\r\nContent-Type: ");
    headers.append(content_type);
  }
  headers.append("\r\nContent-Length: ");
  headers.append(static_cast<std::uint64_t>(body_bytes));
  headers.append("\r\nConnection: close\r\n\r\n");
  // The header buffer is sized for the longest of these, so a truncation here
  // would mean the response says one thing and carries another.
  VOLT_ASSERT(!headers.truncated(), "the response headers did not fit their buffer");
  return headers.view();
}

core::expected<std::size_t> MetricsServer::send_some(pal::IStreamSocket &connection,
                                                     std::string_view payload,
                                                     std::size_t sent) noexcept {
  const std::span<const std::byte> remaining{
      reinterpret_cast<const std::byte *>(payload.data()) + sent, payload.size() - sent};
  const core::expected<std::size_t> written = connection.send(remaining);
  VOLT_TRY(written);
  if (*written == 0) {
    // A stream that accepts nothing is a peer that stopped reading, and
    // retrying it forever would hold the one serving thread against a scraper
    // that has already gone away.
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }
  return *written;
}

core::expected<void> MetricsServer::send_all(pal::IStreamSocket &connection,
                                             std::string_view payload) noexcept {
  std::size_t sent = 0;
  VOLT_LOOP_BOUND(kMaxExpositionBytes);
  while (sent < payload.size()) {
    // Checked by hand rather than with VOLT_TRY, which expands into a loop of
    // its own and would put this body past the nesting limit of AGENTS.md 3.11.
    const core::expected<std::size_t> written = send_some(connection, payload, sent);
    if (!written.has_value()) {
      return std::unexpected{written.error()};
    }
    sent += *written;
  }
  return {};
}

std::uint64_t MetricsServer::scrapes_served() const noexcept {
  return scrapes_served_.load(std::memory_order_relaxed);
}

std::uint64_t MetricsServer::requests_rejected() const noexcept {
  return requests_rejected_.load(std::memory_order_relaxed);
}

std::uint64_t MetricsServer::expositions_truncated() const noexcept {
  return expositions_truncated_.load(std::memory_order_relaxed);
}

} // namespace volt::metrics
