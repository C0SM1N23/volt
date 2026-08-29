#include "sim_socket.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace volt::pal::sim {

core::expected<void> SimSocket::bind(Endpoint local) noexcept {
  const core::expected<Endpoint> bound = world_->network().bind(identifier_, local);
  if (!bound.has_value()) {
    return std::unexpected{bound.error()};
  }
  world_->record("socket.bind", bound->port);
  return {};
}

core::expected<std::size_t> SimSocket::send_to(std::span<const std::byte> payload,
                                               Endpoint destination) noexcept {
  // An unbound sender is given an endpoint here, the way the kernel binds one
  // implicitly on the first send, so a reply always has somewhere to go.
  core::expected<Endpoint> from = world_->network().local_endpoint(identifier_);
  if (!from.has_value()) {
    from = world_->network().bind(identifier_, Endpoint{.address = destination.address, .port = 0});
    if (!from.has_value()) {
      return std::unexpected{from.error()};
    }
  }

  const std::optional<std::int64_t> delivery =
      world_->network().send(*from, destination, payload, world_->now_ns());
  if (!delivery.has_value()) {
    // A dropped datagram is still a successful send: the sender of a datagram
    // is never told it failed to arrive, which is the property the protocols
    // above have to cope with.
    world_->record("socket.drop", static_cast<std::uint64_t>(payload.size()));
    return payload.size();
  }
  world_->record("socket.send", static_cast<std::uint64_t>(*delivery));
  return payload.size();
}

core::expected<Datagram> SimSocket::receive_from(std::span<std::byte> buffer) noexcept {
  const std::optional<std::int64_t> next = world_->network().next_delivery(identifier_);
  const std::int64_t deadline = receive_timeout_.has_value()
                                    ? world_->now_ns() + receive_timeout_->ns()
                                    : std::numeric_limits<std::int64_t>::max();

  if (!next.has_value() || *next > deadline) {
    // Nothing is on its way before the deadline. With no timeout set there is
    // also no later sender, because a simulated world runs one thread: waiting
    // would be a deadlock, so it is reported as a timeout instead. The clock
    // only moves when a real deadline was given; moving it to the end of time
    // would leave every later event unreachable.
    if (receive_timeout_.has_value()) {
      world_->advance_to(deadline);
    }
    world_->record("socket.timeout", static_cast<std::uint64_t>(world_->now_ns()));
    return std::unexpected{core::ErrorCode::kTransientTimeout};
  }

  world_->advance_to(*next);
  std::optional<detail::SimDatagram> datagram =
      world_->network().take_due(identifier_, world_->now_ns());
  if (!datagram.has_value()) {
    return std::unexpected{core::ErrorCode::kTransientTimeout};
  }

  // A datagram longer than the buffer is truncated and the rest discarded,
  // which is what a datagram socket does.
  const std::size_t copied = std::min(buffer.size(), datagram->payload.size());
  std::copy_n(datagram->payload.begin(), copied, buffer.begin());
  world_->record("socket.receive", datagram->sequence);
  return Datagram{.bytes = copied, .from = datagram->from};
}

core::expected<Endpoint> SimSocket::local_endpoint() const noexcept {
  return world_->network().local_endpoint(identifier_);
}

core::expected<void> SimSocket::set_receive_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  receive_timeout_ = timeout;
  return {};
}

} // namespace volt::pal::sim
