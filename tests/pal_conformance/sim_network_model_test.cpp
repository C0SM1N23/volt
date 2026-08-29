#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace volt::pal::sim {
namespace {

constexpr std::uint32_t kLoopbackAddress = 0x7F00'0001U;
constexpr int kDatagramCount = 8;

// Enough jitter next to the latency that the delays drawn for two datagrams
// routinely cross, which is what makes them arrive out of order.
constexpr core::Duration kLatency = core::Duration::from_ms(1);
constexpr core::Duration kJitter = core::Duration::from_ms(10);

// Comfortably past latency plus jitter, so nothing times out for want of
// waiting and a missing datagram means the network really lost track of it.
constexpr core::Duration kGenerousTimeout = core::Duration::from_ms(500);

// Written down so a failure replays exactly; this one reorders the burst below.
constexpr std::uint64_t kReorderingSeed = 0x00A1'B2C3'D4E5'F601ULL;

[[nodiscard]] std::unique_ptr<ISocket> bound_socket(SimPlatform &platform) {
  std::unique_ptr<ISocket> socket = std::move(*platform.create_datagram_socket());
  EXPECT_TRUE(socket->bind(Endpoint{.address = kLoopbackAddress, .port = 0}).has_value());
  return socket;
}

/// Sends `kDatagramCount` datagrams whose single byte is their send position.
void send_numbered_burst(ISocket &sender, Endpoint destination) {
  for (int index = 0; index < kDatagramCount; ++index) {
    const std::array<std::byte, 1> payload{std::byte{static_cast<unsigned char>(index)}};
    EXPECT_TRUE(sender.send_to(payload, destination).has_value());
  }
}

/// Reads every datagram and returns the send positions in the order they came.
[[nodiscard]] std::vector<int> receive_order(ISocket &receiver) {
  std::vector<int> order;
  std::array<std::byte, 4> buffer{};
  for (int index = 0; index < kDatagramCount; ++index) {
    const core::expected<Datagram> received = receiver.receive_from(buffer);
    EXPECT_TRUE(received.has_value()) << "datagram " << index << " never arrived";
    if (received.has_value()) {
      order.push_back(static_cast<int>(std::to_integer<std::uint8_t>(buffer[0])));
    }
  }
  return order;
}

[[nodiscard]] SimConfig jittery_config(std::uint32_t loss_per_million) {
  return SimConfig{.seed = kReorderingSeed,
                   .network = NetworkModel{.latency = kLatency,
                                           .jitter = kJitter,
                                           .loss_per_million = loss_per_million}};
}

TEST(SimNetworkModelTest, DeliversInArrivalOrderRatherThanSendOrder) {
  // The property under test is that a datagram becomes visible when its own
  // delay says so, independently of when it was handed over. A network that
  // delivered strictly in send order would hold back an early arrival behind a
  // late one, and every protocol above would then be tested against an
  // ordering guarantee the real network does not give.
  SimPlatform platform{jittery_config(0)};
  const std::unique_ptr<ISocket> sender = bound_socket(platform);
  const std::unique_ptr<ISocket> receiver = bound_socket(platform);
  ASSERT_TRUE(receiver->set_receive_timeout(kGenerousTimeout).has_value());

  send_numbered_burst(*sender, *receiver->local_endpoint());
  const std::vector<int> order = receive_order(*receiver);

  ASSERT_EQ(order.size(), static_cast<std::size_t>(kDatagramCount));
  EXPECT_FALSE(std::ranges::is_sorted(order)) << "seed " << kReorderingSeed << " did not reorder";
}

TEST(SimNetworkModelTest, LosslessModelDeliversEveryDatagram) {
  SimPlatform platform{jittery_config(0)};
  const std::unique_ptr<ISocket> sender = bound_socket(platform);
  const std::unique_ptr<ISocket> receiver = bound_socket(platform);
  ASSERT_TRUE(receiver->set_receive_timeout(kGenerousTimeout).has_value());

  send_numbered_burst(*sender, *receiver->local_endpoint());
  const std::vector<int> order = receive_order(*receiver);

  std::vector<int> sorted = order;
  std::ranges::sort(sorted);
  EXPECT_EQ(sorted, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
}

TEST(SimNetworkModelTest, LossModelDropsSomeDatagrams) {
  // Every datagram lost, so the check does not depend on which ones the seed
  // happens to pick: a receive must then always report a timeout.
  constexpr std::uint32_t kEverythingLost = 1'000'000;
  SimPlatform platform{jittery_config(kEverythingLost)};
  const std::unique_ptr<ISocket> sender = bound_socket(platform);
  const std::unique_ptr<ISocket> receiver = bound_socket(platform);
  ASSERT_TRUE(receiver->set_receive_timeout(core::Duration::from_ms(20)).has_value());

  send_numbered_burst(*sender, *receiver->local_endpoint());

  std::array<std::byte, 4> buffer{};
  const core::expected<Datagram> received = receiver->receive_from(buffer);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(received.error(), core::ErrorCode::kTransientTimeout);
}

TEST(SimNetworkModelTest, SendingToNobodyStillSucceeds) {
  // A datagram sender is never told its message went nowhere, which is the
  // property every protocol above has to be written against.
  SimPlatform platform{jittery_config(0)};
  const std::unique_ptr<ISocket> sender = bound_socket(platform);

  constexpr std::array<std::byte, 1> kPayload{std::byte{1}};
  const Endpoint nobody{.address = kLoopbackAddress, .port = 1};
  EXPECT_TRUE(sender->send_to(kPayload, nobody).has_value());
}

} // namespace
} // namespace volt::pal::sim
