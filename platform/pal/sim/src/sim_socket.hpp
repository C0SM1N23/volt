#pragma once

#include "sim_network.hpp"
#include "sim_world.hpp"

#include "volt/pal/socket.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace volt::pal::sim {

/// A datagram socket backed by an in-memory queue.
///
/// Receiving moves the clock to the instant the next datagram becomes visible,
/// or to the receive deadline when nothing will arrive. Waiting therefore
/// costs nothing, and the order datagrams arrive in is decided entirely by the
/// seeded network model rather than by how fast the machine happens to run.
class SimSocket final : public ISocket {
public:
  /// @pre `world` outlives this socket
  SimSocket(detail::SimWorld &world, detail::SimNetwork::SocketId identifier) noexcept
      : world_{&world}, identifier_{identifier} {}

  [[nodiscard]] core::expected<void> bind(Endpoint local) noexcept override;
  [[nodiscard]] core::expected<std::size_t> send_to(std::span<const std::byte> payload,
                                                    Endpoint destination) noexcept override;
  [[nodiscard]] core::expected<Datagram>
  receive_from(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<Endpoint> local_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;

private:
  detail::SimWorld *world_;
  detail::SimNetwork::SocketId identifier_;
  std::optional<core::Duration> receive_timeout_;
};

} // namespace volt::pal::sim
