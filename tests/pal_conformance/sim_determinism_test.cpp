#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace volt::pal::sim {
namespace {

// A lossy, jittery network, because a model that never drops or reorders would
// make the digest agree for the trivial reason that nothing random happened.
// One in five lost and up to 3 ms of jitter is enough to reorder datagrams and
// to lose some of them within the handful this scenario sends.
constexpr std::uint32_t kLossPerMillion = 200'000;
constexpr core::Duration kLatency = core::Duration::from_ms(1);
constexpr core::Duration kJitter = core::Duration::from_ms(3);

constexpr std::uint32_t kLoopbackAddress = 0x7F00'0001U;
constexpr int kDatagramCount = 8;
constexpr int kTimerTicks = 3;
constexpr int kThreadCount = 3;

/// Number of repeats the prompt's acceptance criterion asks for.
constexpr int kRepeats = 100;

[[nodiscard]] SimConfig scenario_config(std::uint64_t seed) {
  return SimConfig{.seed = seed,
                   .network = NetworkModel{.latency = kLatency,
                                           .jitter = kJitter,
                                           .loss_per_million = kLossPerMillion}};
}

/// Binds a datagram socket to an ephemeral loopback port.
[[nodiscard]] std::unique_ptr<ISocket> bound_socket(SimPlatform &platform) {
  std::unique_ptr<ISocket> socket = std::move(*platform.create_datagram_socket());
  EXPECT_TRUE(socket->bind(Endpoint{.address = kLoopbackAddress, .port = 0}).has_value());
  return socket;
}

/// Sends a burst of datagrams, which is where the model draws its drops and
/// its per-datagram delay.
void send_burst(ISocket &sender, Endpoint destination) {
  for (int index = 0; index < kDatagramCount; ++index) {
    const std::array<std::byte, 2> payload{std::byte{static_cast<unsigned char>(index)},
                                           std::byte{0}};
    EXPECT_TRUE(sender.send_to(payload, destination).has_value());
  }
}

/// Reads as many times as were sent, which also walks the timeout path
/// whenever the model dropped one; a timeout moves the clock.
void drain_inbox(ISocket &receiver) {
  std::array<std::byte, 8> buffer{};
  for (int index = 0; index < kDatagramCount; ++index) {
    const core::expected<Datagram> received = receiver.receive_from(buffer);
    // Either outcome belongs here: the model drops some of these on purpose,
    // and a delivery and a timeout are both events the digest has to cover.
    EXPECT_TRUE(received.has_value() || received.error() == core::ErrorCode::kTransientTimeout);
  }
}

/// Puts the network model through both of its random decisions.
void exercise_network(SimPlatform &platform) {
  const std::unique_ptr<ISocket> sender = bound_socket(platform);
  const std::unique_ptr<ISocket> receiver = bound_socket(platform);
  EXPECT_TRUE(receiver->set_receive_timeout(core::Duration::from_ms(50)).has_value());

  send_burst(*sender, *receiver->local_endpoint());
  drain_inbox(*receiver);
}

/// Runs a periodic timer, which is what moves the clock in fixed steps.
void exercise_timer(SimPlatform &platform) {
  std::unique_ptr<ITimer> timer = std::move(*platform.create_timer());
  EXPECT_TRUE(timer->arm_periodic(core::Duration::from_ms(2)).has_value());
  for (int tick = 0; tick < kTimerTicks; ++tick) {
    EXPECT_TRUE(timer->wait().has_value());
  }
}

/// What one simulated worker does: append a byte and then wait a little, so
/// both the file order and the clock depend on the order threads ran in.
void append_marker(SimPlatform &platform, int index) {
  const std::array<std::byte, 1> payload{std::byte{static_cast<unsigned char>(index)}};
  std::unique_ptr<IFile> file =
      std::move(*platform.open_file("/sim/determinism.bin", FileMode::kAppend));
  EXPECT_TRUE(file->write(payload).has_value());
  EXPECT_TRUE(platform.clock().sleep_for(core::Duration::from_us(250)).has_value());
}

/// Runs several threads that write to one file and sleep, so both the
/// scheduler's order and the filesystem end up in the digest.
void exercise_threads(SimPlatform &platform) {
  std::vector<std::unique_ptr<IThread>> threads;
  for (int index = 0; index < kThreadCount; ++index) {
    core::expected<std::unique_ptr<IThread>> thread = platform.create_thread(
        ThreadConfig{.name = "sim-worker"}, [&platform, index] { append_marker(platform, index); });
    EXPECT_TRUE(thread.has_value());
    threads.push_back(std::move(*thread));
  }
  for (const std::unique_ptr<IThread> &thread : threads) {
    EXPECT_TRUE(thread->join().has_value());
  }
}

/// Exercises every part of the world that can make a decision, then returns
/// the digest of everything that happened.
///
/// Deliberately touches the clock, the scheduler, the network model and the
/// filesystem: a world can be deterministic in one of them and not in another,
/// and a scenario that used only one would not notice.
[[nodiscard]] std::uint64_t run_scenario(std::uint64_t seed) {
  SimPlatform platform{scenario_config(seed)};
  exercise_network(platform);
  exercise_timer(platform);
  exercise_threads(platform);
  return platform.event_digest();
}

TEST(SimDeterminismTest, TheSameSeedProducesTheSameEventsOneHundredTimes) {
  const std::uint64_t reference = run_scenario(1);

  for (int repeat = 0; repeat < kRepeats; ++repeat) {
    ASSERT_EQ(run_scenario(1), reference) << "run " << repeat << " diverged from the first";
  }
}

TEST(SimDeterminismTest, DifferentSeedsProduceDifferentEvents) {
  // Not a guarantee of the hash, but a check that the seed is actually wired
  // into the decisions: if every seed gave one digest, the run would be
  // reproducible and meaningless at the same time.
  std::set<std::uint64_t> digests;
  constexpr std::uint64_t kSeedsTried = 16;
  for (std::uint64_t seed = 1; seed <= kSeedsTried; ++seed) {
    digests.insert(run_scenario(seed));
  }
  EXPECT_GT(digests.size(), 1U);
}

TEST(SimDeterminismTest, TheDigestDependsOnTheOrderEventsHappenedIn) {
  // Two worlds doing the same things in a different order must not agree. A
  // digest that only summarised the set of events would call two different
  // histories identical, and the determinism check above would then pass over
  // a world that had stopped being deterministic.
  SimPlatform first{scenario_config(11)};
  exercise_timer(first);
  exercise_threads(first);

  SimPlatform second{scenario_config(11)};
  exercise_threads(second);
  exercise_timer(second);

  EXPECT_NE(first.event_digest(), second.event_digest());
}

TEST(SimDeterminismTest, TheDigestCoversEarlierEventsNotJustTheLastOne) {
  // Both worlds end on the very same event; only what happened before it
  // differs. A digest that reflected just the latest event would call these
  // two histories identical, and the check above would then keep passing over
  // a world whose earlier behaviour had drifted.
  SimPlatform with_extra{scenario_config(11)};
  const std::unique_ptr<ISharedMemory> region =
      std::move(*with_extra.create_shared_memory("extra", 64));
  EXPECT_EQ(region->bytes().size(), 64U);
  exercise_timer(with_extra);

  SimPlatform without_extra{scenario_config(11)};
  exercise_timer(without_extra);

  EXPECT_NE(with_extra.event_digest(), without_extra.event_digest());
}

TEST(SimDeterminismTest, TheClockNeverReadsTheHost) {
  // Two worlds built with the same origin read the same time, which a clock
  // that consulted the host could not do.
  SimPlatform first{scenario_config(7)};
  SimPlatform second{scenario_config(7)};

  EXPECT_EQ(first.clock().monotonic(), second.clock().monotonic());
  EXPECT_EQ(first.clock().realtime(), second.clock().realtime());
}

TEST(SimDeterminismTest, TimeOnlyMovesWhenSomeoneWaits) {
  SimPlatform platform{scenario_config(7)};
  const core::Timestamp before = platform.clock().monotonic();

  std::unique_ptr<ISharedMemory> region = std::move(*platform.create_shared_memory("region", 64));
  EXPECT_EQ(region->bytes().size(), 64U);

  EXPECT_EQ(platform.clock().monotonic(), before);
}

} // namespace
} // namespace volt::pal::sim
