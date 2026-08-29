#pragma once

#include "sim_random.hpp"

#include "volt/core/error.hpp"
#include "volt/pal/sim/sim_config.hpp"
#include "volt/pal/socket.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace volt::pal::sim::detail {

/// One datagram waiting to be delivered.
struct SimDatagram {
  std::int64_t deliver_at_ns = 0;
  /// Send order, which breaks ties between datagrams due at the same instant
  /// so delivery order never depends on how a container happens to store them.
  std::uint64_t sequence = 0;
  Endpoint from;
  std::vector<std::byte> payload;
};

/// The whole network, as queues in memory.
///
/// Every datagram is stamped with the time it becomes visible instead of being
/// held back by a timer, so the receiver decides when to look and the clock
/// only moves when someone waits. That is what lets a simulated hour finish in
/// a second.
class SimNetwork final {
public:
  using SocketId = std::uint64_t;

  /// Borrows the world's generator so every network decision comes from the
  /// same seeded stream as everything else.
  ///
  /// @pre `random` outlives this network
  SimNetwork(SimRandom &random, NetworkModel model) noexcept : random_{&random}, model_{model} {}

  /// Creates an unbound socket.
  [[nodiscard]] SocketId open();

  /// Binds `socket` to `requested`, assigning a port when it asks for zero.
  ///
  /// @errors kResourceBusy when the endpoint is already bound
  [[nodiscard]] core::expected<Endpoint> bind(SocketId socket, Endpoint requested);

  /// Returns where `socket` is bound.
  ///
  /// @errors kResourceUnavailable when the socket is not bound
  [[nodiscard]] core::expected<Endpoint> local_endpoint(SocketId socket) const;

  /// Offers a datagram to the network.
  ///
  /// @post   returns the instant it becomes visible, or nothing when the model
  ///         dropped it; a drop is a successful send, as it is on a real network
  [[nodiscard]] std::optional<std::int64_t> send(Endpoint from, Endpoint destination,
                                                 std::span<const std::byte> payload,
                                                 std::int64_t now_ns);

  /// Returns when the next datagram for `socket` becomes visible.
  [[nodiscard]] std::optional<std::int64_t> next_delivery(SocketId socket) const;

  /// Removes and returns the earliest datagram already visible at `now_ns`.
  [[nodiscard]] std::optional<SimDatagram> take_due(SocketId socket, std::int64_t now_ns);

private:
  struct Socket {
    std::optional<Endpoint> local;
    std::vector<SimDatagram> inbox;
  };

  [[nodiscard]] Socket *find_by_endpoint(Endpoint endpoint);
  [[nodiscard]] std::uint16_t next_free_port();

  // First port of the IANA dynamic range, so a simulated ephemeral port looks
  // like the ones the POSIX backend gets from the kernel.
  static constexpr std::uint16_t kFirstEphemeralPort = 49152;

  SimRandom *random_;
  NetworkModel model_;
  std::map<SocketId, Socket> sockets_;
  SocketId next_socket_ = 1;
  std::uint64_t next_sequence_ = 1;
  std::uint16_t next_port_ = kFirstEphemeralPort;
};

} // namespace volt::pal::sim::detail
