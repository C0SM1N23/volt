#pragma once

#include "sim_network.hpp"
#include "sim_world.hpp"

#include "volt/pal/stream_socket.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace volt::pal::sim {

/// One end of a simulated byte stream.
///
/// The two directions are separate queues, so half-closing one does not stop
/// the other. That is the property a request/response protocol relies on when
/// it says "I have finished asking" and then reads the answer.
class SimStreamSocket final : public IStreamSocket {
public:
  /// @pre `world` outlives this socket
  SimStreamSocket(detail::SimWorld &world, detail::SimNetwork::ConnectionId connection,
                  detail::StreamSide side) noexcept
      : world_{&world}, connection_{connection}, side_{side} {}

  [[nodiscard]] core::expected<std::size_t>
  send(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<void> shutdown_send() noexcept override;
  [[nodiscard]] core::expected<Endpoint> peer_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;

private:
  detail::SimWorld *world_;
  detail::SimNetwork::ConnectionId connection_;
  detail::StreamSide side_;
  std::optional<core::Duration> receive_timeout_;
};

} // namespace volt::pal::sim
