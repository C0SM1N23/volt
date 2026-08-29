#include "volt/metrics/counter.hpp"
#include "volt/metrics/gauge.hpp"
#include "volt/metrics/histogram.hpp"
#include "volt/metrics/label.hpp"
#include "volt/metrics/metric_registry.hpp"
#include "volt/metrics/metric_spec.hpp"
#include "volt/metrics/metrics_server.hpp"
#include "volt/metrics/prometheus_exporter.hpp"

#include "volt/pal/file.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/process.hpp"
#include "volt/pal/socket.hpp"
#include "volt/pal/stream_socket.hpp"

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace volt::metrics {
namespace {

// 127.0.0.1 in host order. Every socket test in VOLT stays on the loopback so
// it needs no network and disturbs nobody else's.
constexpr std::uint32_t kLoopback = 0x7F00'0001;

// A zero port asks the platform for an unused one, which the listener reports.
constexpr pal::Endpoint kEphemeralLoopback{.address = kLoopback, .port = 0};

constexpr std::uint64_t kHistogramRange = 100'000;
using TestHistogram = Histogram<kHistogramRange, 3>;

// Room for a whole HTTP response in this file's tests.
constexpr std::size_t kResponseBytes = std::size_t{16} * 1024;

/// A registry holding one of each metric shape, with labels and a tail.
class SampleMetrics final {
public:
  SampleMetrics() {
    scrapes_.add(17);
    load_.set(0.375);
    VOLT_ASSERT(latency_.record(120).has_value(), "a value inside the range was refused");
    VOLT_ASSERT(latency_.record(3400).has_value(), "a value inside the range was refused");
    VOLT_ASSERT(!latency_.record(kHistogramRange + 1).has_value(),
                "a value above the range was accepted");
    VOLT_ASSERT(registry_.add(scrapes_).has_value(), "the counter did not register");
    VOLT_ASSERT(registry_.add(load_).has_value(), "the gauge did not register");
    VOLT_ASSERT(registry_.add(latency_).has_value(), "the histogram did not register");
  }

  [[nodiscard]] const MetricRegistry &registry() const noexcept { return registry_; }

private:
  static constexpr std::array<Label, 1> kNodeLabel{Label{.name = "node", .value = "a\"b"}};

  MetricRegistry registry_;
  Counter scrapes_{MetricSpec{.name = "volt_test_scrapes_total",
                              .help = "Scrapes answered by this node.",
                              .labels = kNodeLabel}};
  Gauge load_{MetricSpec{.name = "volt_test_bus_load_ratio",
                         .help = "Share of the bus in use, from zero to one.",
                         .labels = kNodeLabel}};
  TestHistogram latency_{MetricSpec{.name = "volt_test_latency_us",
                                    .help = "How long a request took, in microseconds.",
                                    .labels = kNodeLabel}};
};

/// Sends `request` to `endpoint` and returns everything the server answers.
[[nodiscard]] std::string exchange(pal::IPlatform &platform, pal::Endpoint endpoint,
                                   std::string_view request) {
  core::expected<std::unique_ptr<pal::IStreamSocket>> connection =
      platform.connect_stream(endpoint);
  if (!connection.has_value()) {
    return {};
  }

  const std::span<const std::byte> payload{reinterpret_cast<const std::byte *>(request.data()),
                                           request.size()};
  if (!(*connection)->send(payload).has_value()) {
    return {};
  }

  std::string answer;
  std::array<std::byte, kResponseBytes> buffer{};
  while (true) {
    const core::expected<std::size_t> received = (*connection)->receive(buffer);
    if (!received.has_value() || *received == 0) {
      break;
    }
    answer.append(reinterpret_cast<const char *>(buffer.data()), *received);
  }
  return answer;
}

/// Returns the body of an HTTP response, or nothing when it has no header end.
[[nodiscard]] std::string_view body_of(std::string_view response) {
  const std::size_t split = response.find("\r\n\r\n");
  if (split == std::string_view::npos) {
    return {};
  }
  return response.substr(split + 4);
}

/// Returns the Content-Length a response declares, or nothing when it has none.
[[nodiscard]] core::expected<std::size_t> declared_length(std::string_view response) {
  constexpr std::string_view kHeader = "Content-Length: ";
  const std::size_t header_at = response.find(kHeader);
  if (header_at == std::string_view::npos) {
    return std::unexpected{core::ErrorCode::kConfigMissingField};
  }
  const std::string_view tail = response.substr(header_at + kHeader.size());
  std::size_t value = 0;
  const std::from_chars_result parsed =
      std::from_chars(tail.data(), tail.data() + tail.size(), value);
  if (parsed.ec != std::errc{}) {
    return std::unexpected{core::ErrorCode::kConfigInvalidValue};
  }
  return value;
}

/// A server bound to an ephemeral loopback port for the life of a test.
class RunningServer final {
public:
  RunningServer(pal::IPlatform &platform, const MetricRegistry &registry)
      : server_{platform, registry} {
    started_ = server_.start(kEphemeralLoopback).has_value();
    const core::expected<pal::Endpoint> endpoint = server_.endpoint();
    if (endpoint.has_value()) {
      endpoint_ = *endpoint;
    }
  }

  ~RunningServer() {
    const core::expected<void> stopped = server_.stop();
    static_cast<void>(stopped.has_value());
  }

  RunningServer(const RunningServer &) = delete;
  RunningServer &operator=(const RunningServer &) = delete;
  RunningServer(RunningServer &&) = delete;
  RunningServer &operator=(RunningServer &&) = delete;

  [[nodiscard]] bool started() const noexcept { return started_ && endpoint_.port != 0; }
  [[nodiscard]] pal::Endpoint endpoint() const noexcept { return endpoint_; }
  [[nodiscard]] MetricsServer &server() noexcept { return server_; }

private:
  MetricsServer server_;
  bool started_ = false;
  pal::Endpoint endpoint_{};
};

TEST(MetricsServerTest, ServesTheRegistryAtTheMetricsPath) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const std::string response =
      exchange(platform, server.endpoint(), "GET /metrics HTTP/1.1\r\nHost: volt\r\n\r\n");

  ASSERT_FALSE(response.empty());
  EXPECT_TRUE(response.starts_with("HTTP/1.1 200 OK\r\n")) << response;
  EXPECT_NE(response.find("Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"),
            std::string::npos);

  const std::string_view body = body_of(response);
  const core::expected<std::size_t> length = declared_length(response);
  ASSERT_TRUE(length.has_value());
  EXPECT_EQ(*length, body.size()) << "the declared length must match what was sent";
  EXPECT_NE(body.find("volt_test_scrapes_total{node=\"a\\\"b\"} 17\n"), std::string_view::npos)
      << body;
  EXPECT_NE(body.find("# TYPE volt_test_latency_us summary\n"), std::string_view::npos) << body;
  EXPECT_EQ(server.server().scrapes_served(), 1U);
  EXPECT_EQ(server.server().requests_rejected(), 0U);
}

TEST(MetricsServerTest, AnswersNotFoundForAnyOtherPath) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const std::string response =
      exchange(platform, server.endpoint(), "GET /healthz HTTP/1.1\r\nHost: volt\r\n\r\n");

  EXPECT_TRUE(response.starts_with("HTTP/1.1 404 Not Found\r\n")) << response;
  EXPECT_TRUE(body_of(response).empty());
  EXPECT_EQ(server.server().scrapes_served(), 0U);
  EXPECT_EQ(server.server().requests_rejected(), 1U);
}

TEST(MetricsServerTest, RefusesAMethodThatIsNotGet) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const std::string response =
      exchange(platform, server.endpoint(),
               "POST /metrics HTTP/1.1\r\nHost: volt\r\nContent-Length: 0\r\n\r\n");

  EXPECT_TRUE(response.starts_with("HTTP/1.1 405 Method Not Allowed\r\n")) << response;
  EXPECT_EQ(server.server().scrapes_served(), 0U);
  EXPECT_EQ(server.server().requests_rejected(), 1U);
}

TEST(MetricsServerTest, RefusesAMethodThatIsAsLongAsGet) {
  // PUT is four characters, so a server that skipped the method check and
  // simply cut four characters off the front would find "/metrics" here and
  // serve it. That is the case that proves the method is actually read.
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const std::string response =
      exchange(platform, server.endpoint(), "PUT /metrics HTTP/1.1\r\nHost: volt\r\n\r\n");

  EXPECT_TRUE(response.starts_with("HTTP/1.1 405 Method Not Allowed\r\n")) << response;
  EXPECT_EQ(server.server().scrapes_served(), 0U);
  EXPECT_EQ(server.server().requests_rejected(), 1U);
}

TEST(MetricsServerTest, KeepsServingAfterARejectedRequest) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const std::string rejected =
      exchange(platform, server.endpoint(), "GET /nowhere HTTP/1.1\r\n\r\n");
  const std::string served = exchange(platform, server.endpoint(), "GET /metrics HTTP/1.1\r\n\r\n");

  EXPECT_TRUE(rejected.starts_with("HTTP/1.1 404")) << rejected;
  EXPECT_TRUE(served.starts_with("HTTP/1.1 200")) << served;
  EXPECT_EQ(server.server().scrapes_served(), 1U);
  EXPECT_EQ(server.server().requests_rejected(), 1U);
}

TEST(MetricsServerTest, ReportsNoEndpointBeforeItIsStarted) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  MetricsServer server{platform, metrics.registry()};

  const core::expected<pal::Endpoint> endpoint = server.endpoint();

  ASSERT_FALSE(endpoint.has_value());
  EXPECT_EQ(endpoint.error(), core::ErrorCode::kResourceUnavailable);
}

TEST(MetricsServerTest, RefusesToStartTwice) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  RunningServer server{platform, metrics.registry()};
  ASSERT_TRUE(server.started());

  const core::expected<void> again = server.server().start(kEphemeralLoopback);

  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kResourceUnavailable);
}

TEST(MetricsServerTest, ReleasesItsPortWhenStopped) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  pal::Endpoint used{};
  {
    MetricsServer first{platform, metrics.registry()};
    ASSERT_TRUE(first.start(kEphemeralLoopback).has_value());
    const core::expected<pal::Endpoint> endpoint = first.endpoint();
    ASSERT_TRUE(endpoint.has_value());
    used = *endpoint;
    ASSERT_TRUE(first.stop().has_value());
  }

  // Binding the very port the first server had is what proves it let go of it.
  MetricsServer second{platform, metrics.registry()};
  const core::expected<void> restarted = second.start(used);

  ASSERT_TRUE(restarted.has_value()) << "the stopped server kept its port";
  EXPECT_TRUE(second.stop().has_value());
}

TEST(MetricsServerTest, StoppingAServerThatNeverStartedIsHarmless) {
  pal::posix::PosixPlatform platform;
  const SampleMetrics metrics;
  MetricsServer server{platform, metrics.registry()};

  EXPECT_TRUE(server.stop().has_value());
  EXPECT_TRUE(server.stop().has_value());
}

} // namespace
} // namespace volt::metrics
