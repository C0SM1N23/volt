// K3: one-way latency and sustained throughput of every intra-node transport
// SPEC 10.1 lists, measured the same way for each - a ping-pong between two
// threads for latency (one way = round trip / 2), a one-direction blast for
// throughput. Numbers land in docs/PERFORMANCE.md next to the methodology.

#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"
#include "volt/memory/seq_lock.hpp"
#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <format>
#include <functional>
#include <print>
#include <string>
#include <vector>

namespace volt::ipc {
namespace {

#if defined(VOLT_INSTRUMENTED)
constexpr bool kInstrumented = true;
#else
constexpr bool kInstrumented = false;
#endif

/// The payload every mechanism carries: 64 bytes, the size SPEC 10.1 uses.
struct Payload64 {
  std::uint64_t sequence = 0;
  std::array<std::uint64_t, 7> body{};
};
static_assert(sizeof(Payload64) == 64);

constexpr std::uint64_t kWarmup = 2'000;
constexpr std::uint64_t kSharedMemoryRounds = 100'000;
constexpr std::uint64_t kSocketRounds = 20'000;
constexpr std::uint64_t kBlastMessages = 1'000'000;

[[nodiscard]] constexpr pal::ThreadConfig thread_config(std::string_view name) noexcept {
  return pal::ThreadConfig{.name = name,
                           .policy = pal::SchedulingPolicy::kOther,
                           .priority = core::Priority{},
                           .cpu_mask = 0,
                           .stack_bytes = 0};
}

struct LatencyReport {
  std::int64_t p50 = 0;
  std::int64_t p99 = 0;
  std::int64_t p999 = 0;
  std::int64_t worst = 0;
};

/// Turns round-trip samples into the one-way row of the K3 table.
[[nodiscard]] LatencyReport summarise(std::vector<std::int64_t> &round_trips) {
  std::ranges::sort(round_trips);
  const auto one_way_at = [&](double quantile) {
    const std::size_t position =
        std::min(round_trips.size() - 1,
                 static_cast<std::size_t>(quantile * static_cast<double>(round_trips.size())));
    return round_trips[position] / 2;
  };
  return LatencyReport{.p50 = one_way_at(0.50),
                       .p99 = one_way_at(0.99),
                       .p999 = one_way_at(0.999),
                       .worst = round_trips.back() / 2};
}

/// One log2 histogram line per occupied bucket, for the distribution plot
/// the spec asks to publish alongside the table.
void print_histogram(std::string_view mechanism, const std::vector<std::int64_t> &samples) {
  std::array<std::uint64_t, 64> buckets{};
  for (const std::int64_t sample : samples) {
    const std::uint64_t one_way = static_cast<std::uint64_t>(sample) / 2U;
    buckets[static_cast<std::size_t>(std::bit_width(one_way))] += 1;
  }
  std::print("histogram {}\n", mechanism);
  for (std::size_t bucket = 0; bucket < buckets.size(); ++bucket) {
    if (buckets[bucket] == 0) {
      continue;
    }
    const std::uint64_t from = bucket == 0 ? 0 : (1ULL << (bucket - 1));
    const std::size_t width =
        static_cast<std::size_t>(1 + ((63 * buckets[bucket]) / samples.size()));
    std::print("  {:>9} ns | {:<63} {}\n", from, std::string(width, '#'), buckets[bucket]);
  }
}

void report(std::string_view mechanism, const LatencyReport &latency, double throughput) {
  std::print(
      "K3 {:<18} P50 {:>7} ns  P99 {:>7} ns  P99.9 {:>8} ns  max {:>9} ns  {:>6.2f} M msg/s\n",
      mechanism, latency.p50, latency.p99, latency.p999, latency.worst, throughput / 1e6);
}

/// Runs `echo` on a second thread, measures `round` on this one.
///
/// The echo side must answer `rounds + warmup` requests, whatever transport
/// it speaks; the measuring side times each round after the warmup.
[[nodiscard]] std::vector<std::int64_t> ping_pong(pal::IPlatform &platform, std::uint64_t rounds,
                                                  std::move_only_function<void()> echo,
                                                  const std::function<void(std::uint64_t)> &round) {
  core::expected<std::unique_ptr<pal::IThread>> responder =
      platform.create_thread(thread_config("volt-bench-echo"), std::move(echo));
  EXPECT_TRUE(responder.has_value());

  pal::IClock &clock = platform.clock();
  std::vector<std::int64_t> samples;
  samples.reserve(rounds);
  for (std::uint64_t iteration = 0; iteration < kWarmup + rounds; ++iteration) {
    const std::int64_t before = clock.monotonic().ns_since_epoch();
    round(iteration);
    const std::int64_t after = clock.monotonic().ns_since_epoch();
    if (iteration >= kWarmup) {
      samples.push_back(after - before);
    }
  }
  EXPECT_TRUE((*responder)->join().has_value());
  return samples;
}

class IpcBenchmarkTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (kInstrumented) {
      GTEST_SKIP() << "a sanitizer or coverage build measures the tool, not the transport";
    }
  }

  [[nodiscard]] std::string unique(std::string_view stem) {
    return std::format("volt-bench-{}-{}", platform_.current_process_id(), stem);
  }

  pal::posix::PosixPlatform platform_;
};

TEST_F(IpcBenchmarkTest, SharedMemoryRing) {
  constexpr TopicConfig kConfig{.slot_count = 64, .history_depth = 8, .max_subscribers = 1};
  core::expected<Topic<Payload64>> forward =
      Topic<Payload64>::create(platform_, unique("fwd"), kConfig);
  core::expected<Topic<Payload64>> reverse =
      Topic<Payload64>::create(platform_, unique("rev"), kConfig);
  ASSERT_TRUE(forward.has_value());
  ASSERT_TRUE(reverse.has_value());
  core::expected<Publisher<Payload64>> ping = forward->publisher();
  core::expected<Subscriber<Payload64>> pong = reverse->subscriber();
  ASSERT_TRUE(ping.has_value());
  ASSERT_TRUE(pong.has_value());
  // The echo endpoints attach here, before the first publish: a publish
  // with no subscriber is legally discarded, and a first message lost to
  // the attach race would leave both sides waiting forever.
  core::expected<Subscriber<Payload64>> request = forward->subscriber();
  core::expected<Publisher<Payload64>> reply = reverse->publisher();
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(reply.has_value());

  std::vector<std::int64_t> samples = ping_pong(
      platform_, kSharedMemoryRounds,
      [request = std::move(*request), reply = std::move(*reply)]() mutable {
        for (std::uint64_t iteration = 0; iteration < kWarmup + kSharedMemoryRounds;) {
          core::expected<Sample<Payload64>> received = request.take();
          if (!received.has_value()) {
            continue;
          }
          core::expected<Loan<Payload64>> loan = reply.loan();
          ASSERT_TRUE(loan.has_value());
          (*loan)->sequence = (*received)->sequence;
          reply.publish(std::move(*loan));
          iteration += 1;
        }
      },
      [&ping, &pong](std::uint64_t iteration) {
        core::expected<Loan<Payload64>> loan = ping->loan();
        ASSERT_TRUE(loan.has_value());
        (*loan)->sequence = iteration;
        ping->publish(std::move(*loan));
        while (true) {
          const core::expected<Sample<Payload64>> answer = pong->take();
          if (answer.has_value()) {
            ASSERT_EQ((*answer)->sequence, iteration);
            break;
          }
        }
      });

  // Throughput: one direction, drained concurrently, counted at the sink.
  core::expected<Topic<Payload64>> blast = Topic<Payload64>::create(
      platform_, unique("blast"),
      TopicConfig{.slot_count = 1024, .history_depth = 512, .max_subscribers = 1});
  ASSERT_TRUE(blast.has_value());
  core::expected<Publisher<Payload64>> source = blast->publisher();
  core::expected<Subscriber<Payload64>> sink = blast->subscriber();
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(sink.has_value());
  std::atomic<bool> done{false};
  std::atomic<std::uint64_t> received{0};
  core::expected<std::unique_ptr<pal::IThread>> drainer =
      platform_.create_thread(thread_config("volt-bench-drain"), [&] {
        while (true) {
          if (sink->take().has_value()) {
            received.fetch_add(1, std::memory_order_relaxed);
            continue;
          }
          if (done.load(std::memory_order_acquire) && sink->pending() == 0) {
            return;
          }
        }
      });
  ASSERT_TRUE(drainer.has_value());
  const std::int64_t blast_start = platform_.clock().monotonic().ns_since_epoch();
  std::uint64_t produced = 0;
  while (produced < kBlastMessages) {
    core::expected<Loan<Payload64>> loan = source->loan();
    if (!loan.has_value()) {
      continue;
    }
    (*loan)->sequence = produced;
    source->publish(std::move(*loan));
    produced += 1;
  }
  done.store(true, std::memory_order_release);
  ASSERT_TRUE((*drainer)->join().has_value());
  const std::int64_t blast_ns = platform_.clock().monotonic().ns_since_epoch() - blast_start;
  const std::uint64_t delivered = received.load() + sink->dropped();
  EXPECT_EQ(delivered, kBlastMessages) << "the blast lost messages off the books";

  const LatencyReport latency = summarise(samples);
  report("shm SPSC ring", latency,
         static_cast<double>(kBlastMessages) * 1e9 / static_cast<double>(blast_ns));
  print_histogram("shm SPSC ring", samples);
  // K3 wants < 2 us / < 8 us on tuned hardware; a shared dev machine gets
  // headroom, not a pass by generosity (AGENTS.md 8.9).
  EXPECT_LT(latency.p50, 5'000);
}

TEST_F(IpcBenchmarkTest, SharedMemorySeqlock) {
  memory::SeqLock<Payload64> forward;
  memory::SeqLock<Payload64> reverse;

  std::vector<std::int64_t> samples = ping_pong(
      platform_, kSharedMemoryRounds,
      [&forward, &reverse] {
        std::uint64_t last = 0;
        for (std::uint64_t iteration = 0; iteration < kWarmup + kSharedMemoryRounds;) {
          const core::expected<Payload64> request = forward.load();
          if (!request.has_value() || request->sequence != iteration + 1) {
            continue;
          }
          Payload64 reply{};
          reply.sequence = request->sequence;
          reverse.store(reply);
          last = request->sequence;
          iteration += 1;
        }
        static_cast<void>(last);
      },
      [&forward, &reverse](std::uint64_t iteration) {
        Payload64 request{};
        request.sequence = iteration + 1;
        forward.store(request);
        while (true) {
          const core::expected<Payload64> reply = reverse.load();
          if (reply.has_value() && reply->sequence == iteration + 1) {
            break;
          }
        }
      });

  // Throughput for a last-value seat is the sustained store rate.
  const std::int64_t store_start = platform_.clock().monotonic().ns_since_epoch();
  Payload64 value{};
  for (std::uint64_t store = 0; store < kBlastMessages; ++store) {
    value.sequence = store;
    forward.store(value);
  }
  const std::int64_t store_ns = platform_.clock().monotonic().ns_since_epoch() - store_start;

  const LatencyReport latency = summarise(samples);
  report("shm seqlock", latency,
         static_cast<double>(kBlastMessages) * 1e9 / static_cast<double>(store_ns));
  EXPECT_LT(latency.p50, 5'000);
}

/// Stream ping-pong shared by the local-socket and TCP rows.
void stream_ping_pong(pal::IPlatform &platform, pal::IStreamSocket &client,
                      std::unique_ptr<pal::IStreamSocket> server, std::string_view mechanism) {
  auto echo = [socket = std::move(server)] {
    std::array<std::byte, sizeof(Payload64)> buffer{};
    for (std::uint64_t iteration = 0; iteration < kWarmup + kSocketRounds; ++iteration) {
      std::size_t filled = 0;
      while (filled < buffer.size()) {
        const core::expected<std::size_t> got = socket->receive(std::span{buffer}.subspan(filled));
        ASSERT_TRUE(got.has_value());
        filled += *got;
      }
      std::size_t sent = 0;
      while (sent < buffer.size()) {
        const core::expected<std::size_t> put =
            socket->send(std::span<const std::byte>{buffer}.subspan(sent));
        ASSERT_TRUE(put.has_value());
        sent += *put;
      }
    }
  };
  std::vector<std::int64_t> samples =
      ping_pong(platform, kSocketRounds, std::move(echo), [&client](std::uint64_t) {
        std::array<std::byte, sizeof(Payload64)> buffer{};
        std::size_t sent = 0;
        while (sent < buffer.size()) {
          const core::expected<std::size_t> put =
              client.send(std::span<const std::byte>{buffer}.subspan(sent));
          ASSERT_TRUE(put.has_value());
          sent += *put;
        }
        std::size_t filled = 0;
        while (filled < buffer.size()) {
          const core::expected<std::size_t> got = client.receive(std::span{buffer}.subspan(filled));
          ASSERT_TRUE(got.has_value());
          filled += *got;
        }
      });

  const LatencyReport latency = summarise(samples);
  // Round-trip cost dominates a stream's sustainable request rate, so the
  // table derives it instead of running a second campaign per socket type.
  report(mechanism, latency, 1e9 / static_cast<double>(2 * latency.p50));
}

TEST_F(IpcBenchmarkTest, UnixDomainSocket) {
  const std::string path = std::format("/tmp/{}.sock", unique("uds"));
  core::expected<std::unique_ptr<pal::IStreamListener>> listener =
      platform_.listen_local(path, pal::kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  core::expected<std::unique_ptr<pal::IStreamSocket>> client = platform_.connect_local(path);
  ASSERT_TRUE(client.has_value());
  core::expected<std::unique_ptr<pal::IStreamSocket>> server = (*listener)->accept();
  ASSERT_TRUE(server.has_value());
  stream_ping_pong(platform_, **client, std::move(*server), "unix domain socket");
}

TEST_F(IpcBenchmarkTest, PosixMessageQueue) {
  core::expected<std::unique_ptr<pal::IMessageQueue>> forward = platform_.create_message_queue(
      pal::MessageQueueConfig{.name = unique("mq-fwd"), .depth = 8, .message_bytes = 64});
  core::expected<std::unique_ptr<pal::IMessageQueue>> reverse = platform_.create_message_queue(
      pal::MessageQueueConfig{.name = unique("mq-rev"), .depth = 8, .message_bytes = 64});
  ASSERT_TRUE(forward.has_value());
  ASSERT_TRUE(reverse.has_value());

  auto echo = [&forward, &reverse] {
    std::array<std::byte, sizeof(Payload64)> buffer{};
    for (std::uint64_t iteration = 0; iteration < kWarmup + kSocketRounds; ++iteration) {
      const core::expected<std::size_t> got = (*forward)->receive(buffer);
      ASSERT_TRUE(got.has_value());
      ASSERT_TRUE((*reverse)->send(std::span<const std::byte>{buffer}).has_value());
    }
  };
  std::vector<std::int64_t> samples =
      ping_pong(platform_, kSocketRounds, std::move(echo), [&forward, &reverse](std::uint64_t) {
        std::array<std::byte, sizeof(Payload64)> buffer{};
        ASSERT_TRUE((*forward)->send(std::span<const std::byte>{buffer}).has_value());
        const core::expected<std::size_t> got = (*reverse)->receive(buffer);
        ASSERT_TRUE(got.has_value());
      });

  const LatencyReport latency = summarise(samples);
  report("POSIX mq", latency, 1e9 / static_cast<double>(2 * latency.p50));
}

TEST_F(IpcBenchmarkTest, TcpLoopback) {
  core::expected<std::unique_ptr<pal::IStreamListener>> listener = platform_.listen_stream(
      pal::Endpoint{.address = 0x7F00'0001, .port = 0}, pal::kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  const core::expected<pal::Endpoint> where = (*listener)->local_endpoint();
  ASSERT_TRUE(where.has_value());
  core::expected<std::unique_ptr<pal::IStreamSocket>> client = platform_.connect_stream(*where);
  ASSERT_TRUE(client.has_value());
  core::expected<std::unique_ptr<pal::IStreamSocket>> server = (*listener)->accept();
  ASSERT_TRUE(server.has_value());
  stream_ping_pong(platform_, **client, std::move(*server), "TCP loopback");
}

TEST_F(IpcBenchmarkTest, UdpLoopback) {
  core::expected<std::unique_ptr<pal::ISocket>> forward = platform_.create_datagram_socket();
  core::expected<std::unique_ptr<pal::ISocket>> reverse = platform_.create_datagram_socket();
  ASSERT_TRUE(forward.has_value());
  ASSERT_TRUE(reverse.has_value());
  constexpr pal::Endpoint kAny{.address = 0x7F00'0001, .port = 0};
  ASSERT_TRUE((*forward)->bind(kAny).has_value());
  ASSERT_TRUE((*reverse)->bind(kAny).has_value());
  const core::expected<pal::Endpoint> forward_at = (*forward)->local_endpoint();
  const core::expected<pal::Endpoint> reverse_at = (*reverse)->local_endpoint();
  ASSERT_TRUE(forward_at.has_value());
  ASSERT_TRUE(reverse_at.has_value());

  auto echo = [&forward, reply_to = *reverse_at] {
    std::array<std::byte, sizeof(Payload64)> buffer{};
    for (std::uint64_t iteration = 0; iteration < kWarmup + kSocketRounds; ++iteration) {
      const core::expected<pal::Datagram> got = (*forward)->receive_from(buffer);
      ASSERT_TRUE(got.has_value());
      ASSERT_TRUE((*forward)->send_to(std::span<const std::byte>{buffer}, reply_to).has_value());
    }
  };
  std::vector<std::int64_t> samples =
      ping_pong(platform_, kSocketRounds, std::move(echo), [&forward_at, &reverse](std::uint64_t) {
        std::array<std::byte, sizeof(Payload64)> buffer{};
        ASSERT_TRUE(
            (*reverse)->send_to(std::span<const std::byte>{buffer}, *forward_at).has_value());
        const core::expected<pal::Datagram> got = (*reverse)->receive_from(buffer);
        ASSERT_TRUE(got.has_value());
      });

  const LatencyReport latency = summarise(samples);
  report("UDP loopback", latency, 1e9 / static_cast<double>(2 * latency.p50));
}

} // namespace
} // namespace volt::ipc
