#include "sim_stream_socket.hpp"

#include <algorithm>

namespace volt::pal::sim {
namespace {

/// Returns the queue `side` writes into, which is the one its peer reads.
[[nodiscard]] std::vector<std::byte> &outbound(detail::StreamConnection &connection,
                                               detail::StreamSide side) noexcept {
  return side == detail::StreamSide::kClient ? connection.to_server : connection.to_client;
}

/// Returns the queue `side` reads from.
[[nodiscard]] std::vector<std::byte> &inbound(detail::StreamConnection &connection,
                                              detail::StreamSide side) noexcept {
  return side == detail::StreamSide::kClient ? connection.to_client : connection.to_server;
}

/// Reports whether `side` has already stopped sending.
[[nodiscard]] bool has_stopped_sending(const detail::StreamConnection &connection,
                                       detail::StreamSide side) noexcept {
  return side == detail::StreamSide::kClient ? connection.client_stopped_sending
                                             : connection.server_stopped_sending;
}

[[nodiscard]] detail::StreamSide other_side(detail::StreamSide side) noexcept {
  return side == detail::StreamSide::kClient ? detail::StreamSide::kServer
                                             : detail::StreamSide::kClient;
}

} // namespace

core::expected<std::size_t> SimStreamSocket::send(std::span<const std::byte> payload) noexcept {
  detail::StreamConnection *const connection = world_->network().connection(connection_);
  if (connection == nullptr) {
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }
  if (has_stopped_sending(*connection, side_)) {
    // Writing after half-closing is the caller contradicting itself, and the
    // peer has already been told no more bytes are coming.
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }

  std::vector<std::byte> &queue = outbound(*connection, side_);
  queue.insert(queue.end(), payload.begin(), payload.end());
  world_->record("stream.send", static_cast<std::uint64_t>(payload.size()));
  return payload.size();
}

core::expected<std::size_t> SimStreamSocket::receive(std::span<std::byte> buffer) noexcept {
  detail::StreamConnection *const connection = world_->network().connection(connection_);
  if (connection == nullptr) {
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }

  std::vector<std::byte> &queue = inbound(*connection, side_);
  if (queue.empty()) {
    if (has_stopped_sending(*connection, other_side(side_))) {
      // End of stream: the peer half-closed and nothing is left to read.
      world_->record("stream.eof", 0);
      return 0;
    }
    // Nothing has arrived, and in a single-threaded world nothing will before
    // control returns here. The clock moves only when a deadline was given.
    if (receive_timeout_.has_value()) {
      world_->advance_by(receive_timeout_->ns());
    }
    world_->record("stream.timeout", static_cast<std::uint64_t>(world_->now_ns()));
    return std::unexpected{core::ErrorCode::kTransientTimeout};
  }

  const std::size_t taken = std::min(buffer.size(), queue.size());
  std::copy_n(queue.begin(), taken, buffer.begin());
  queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(taken));
  world_->record("stream.receive", static_cast<std::uint64_t>(taken));
  return taken;
}

core::expected<void> SimStreamSocket::shutdown_send() noexcept {
  detail::StreamConnection *const connection = world_->network().connection(connection_);
  if (connection == nullptr) {
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }
  if (side_ == detail::StreamSide::kClient) {
    connection->client_stopped_sending = true;
  } else {
    connection->server_stopped_sending = true;
  }
  world_->record("stream.shutdown", static_cast<std::uint64_t>(connection_));
  return {};
}

core::expected<Endpoint> SimStreamSocket::peer_endpoint() const noexcept {
  detail::StreamConnection *const connection = world_->network().connection(connection_);
  if (connection == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return side_ == detail::StreamSide::kClient ? connection->server : connection->client;
}

core::expected<void> SimStreamSocket::set_receive_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  receive_timeout_ = timeout;
  return {};
}

} // namespace volt::pal::sim
