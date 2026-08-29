#include "sim_network.hpp"

#include <algorithm>
#include <utility>

namespace volt::pal::sim::detail {
namespace {

// A probability is expressed out of this many parts, matching the unit of
// NetworkModel::loss_per_million.
constexpr std::uint64_t kPartsPerMillion = 1'000'000;

} // namespace

SimNetwork::SocketId SimNetwork::open() {
  const SocketId socket = next_socket_;
  next_socket_ += 1;
  sockets_.emplace(socket, Socket{});
  return socket;
}

SimNetwork::Socket *SimNetwork::find_by_endpoint(Endpoint endpoint) {
  for (auto &[socket, state] : sockets_) {
    if (state.local.has_value() && *state.local == endpoint) {
      return &state;
    }
  }
  return nullptr;
}

std::uint16_t SimNetwork::next_free_port() {
  const std::uint16_t port = next_port_;
  next_port_ += 1;
  return port;
}

core::expected<Endpoint> SimNetwork::bind(SocketId socket, Endpoint requested) {
  const auto entry = sockets_.find(socket);
  if (entry == sockets_.end()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  if (entry->second.local.has_value()) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }

  Endpoint chosen = requested;
  if (chosen.port == 0) {
    chosen.port = next_free_port();
  } else if (find_by_endpoint(chosen) != nullptr) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  entry->second.local = chosen;
  return chosen;
}

core::expected<Endpoint> SimNetwork::local_endpoint(SocketId socket) const {
  const auto entry = sockets_.find(socket);
  if (entry == sockets_.end()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  const std::optional<Endpoint> &local = entry->second.local;
  if (!local.has_value()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return *local;
}

std::optional<std::int64_t> SimNetwork::send(Endpoint from, Endpoint destination,
                                             std::span<const std::byte> payload,
                                             std::int64_t now_ns) {
  // The draw happens whether or not anyone is listening, so adding a receiver
  // to a scenario does not shift every later random decision.
  const bool dropped =
      model_.loss_per_million > 0 &&
      random_->next_below(kPartsPerMillion) < static_cast<std::uint64_t>(model_.loss_per_million);
  const std::int64_t extra_delay = static_cast<std::int64_t>(random_->next_below(
      static_cast<std::uint64_t>(std::max<std::int64_t>(model_.jitter.ns(), 0) + 1)));

  Socket *inbox_owner = find_by_endpoint(destination);
  if (dropped || inbox_owner == nullptr) {
    return std::nullopt;
  }

  const std::int64_t deliver_at = now_ns + model_.latency.ns() + extra_delay;
  inbox_owner->inbox.push_back(SimDatagram{.deliver_at_ns = deliver_at,
                                           .sequence = next_sequence_,
                                           .from = from,
                                           .payload = {payload.begin(), payload.end()}});
  next_sequence_ += 1;
  return deliver_at;
}

std::optional<std::int64_t> SimNetwork::next_delivery(SocketId socket) const {
  const auto entry = sockets_.find(socket);
  if (entry == sockets_.end() || entry->second.inbox.empty()) {
    return std::nullopt;
  }
  const auto earliest =
      std::ranges::min_element(entry->second.inbox, {}, &SimDatagram::deliver_at_ns);
  return earliest->deliver_at_ns;
}

std::optional<SimDatagram> SimNetwork::take_due(SocketId socket, std::int64_t now_ns) {
  const auto entry = sockets_.find(socket);
  if (entry == sockets_.end()) {
    return std::nullopt;
  }
  std::vector<SimDatagram> &inbox = entry->second.inbox;

  // Earliest visible datagram, with send order breaking a tie so two datagrams
  // due at the same instant always arrive the same way round.
  const auto chosen =
      std::ranges::min_element(inbox, [](const SimDatagram &left, const SimDatagram &right) {
        return std::tie(left.deliver_at_ns, left.sequence) <
               std::tie(right.deliver_at_ns, right.sequence);
      });
  if (chosen == inbox.end() || chosen->deliver_at_ns > now_ns) {
    return std::nullopt;
  }

  SimDatagram datagram = std::move(*chosen);
  inbox.erase(chosen);
  return datagram;
}

SimNetwork::Listener *SimNetwork::find_listener(Endpoint endpoint) {
  for (auto &[socket, listener] : listeners_) {
    if (listener.local == endpoint) {
      return &listener;
    }
  }
  return nullptr;
}

core::expected<SimNetwork::SocketId> SimNetwork::listen(Endpoint local, unsigned backlog) {
  Endpoint chosen = local;
  if (chosen.port == 0) {
    chosen.port = next_free_port();
  } else if (find_listener(chosen) != nullptr || find_by_endpoint(chosen) != nullptr) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }

  const SocketId listener = next_socket_;
  next_socket_ += 1;
  listeners_.emplace(listener, Listener{.local = chosen, .backlog = backlog, .pending = {}});
  return listener;
}

core::expected<Endpoint> SimNetwork::listener_endpoint(SocketId listener) const {
  const auto entry = listeners_.find(listener);
  if (entry == listeners_.end()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return entry->second.local;
}

core::expected<SimNetwork::ConnectionId> SimNetwork::connect(Endpoint remote) {
  Listener *const listener = find_listener(remote);
  if (listener == nullptr) {
    return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
  }
  if (listener->pending.size() >= listener->backlog) {
    // A full backlog is the listener failing to keep up, which the caller must
    // see rather than have hidden behind an unbounded queue.
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }

  const ConnectionId identifier = next_connection_;
  next_connection_ += 1;
  connections_.emplace(identifier, StreamConnection{.client = Endpoint{.address = remote.address,
                                                                       .port = next_free_port()},
                                                    .server = remote,
                                                    .to_client = {},
                                                    .to_server = {},
                                                    .client_stopped_sending = false,
                                                    .server_stopped_sending = false});
  listener->pending.push_back(identifier);
  return identifier;
}

std::optional<SimNetwork::ConnectionId> SimNetwork::take_pending(SocketId listener) {
  const auto entry = listeners_.find(listener);
  if (entry == listeners_.end() || entry->second.pending.empty()) {
    return std::nullopt;
  }
  const ConnectionId identifier = entry->second.pending.front();
  entry->second.pending.pop_front();
  return identifier;
}

StreamConnection *SimNetwork::connection(ConnectionId identifier) {
  const auto entry = connections_.find(identifier);
  if (entry == connections_.end()) {
    return nullptr;
  }
  return &entry->second;
}

} // namespace volt::pal::sim::detail
