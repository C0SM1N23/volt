#include "sim_stream_listener.hpp"

#include "sim_stream_socket.hpp"

namespace volt::pal::sim {

core::expected<std::unique_ptr<IStreamSocket>> SimStreamListener::accept() noexcept {
  const std::optional<detail::SimNetwork::ConnectionId> pending =
      world_->network().take_pending(listener_);
  if (!pending.has_value()) {
    // Nobody is waiting, and in a single-threaded world nobody will arrive
    // before control returns here, so waiting out the deadline is the whole
    // wait rather than a first attempt.
    if (accept_timeout_.has_value()) {
      world_->advance_by(accept_timeout_->ns());
    }
    world_->record("listener.timeout", static_cast<std::uint64_t>(world_->now_ns()));
    return std::unexpected{core::ErrorCode::kTransientTimeout};
  }

  world_->record("listener.accept", *pending);
  return std::make_unique<SimStreamSocket>(*world_, *pending, detail::StreamSide::kServer);
}

core::expected<Endpoint> SimStreamListener::local_endpoint() const noexcept {
  return world_->network().listener_endpoint(listener_);
}

core::expected<void> SimStreamListener::set_accept_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  accept_timeout_ = timeout;
  return {};
}

} // namespace volt::pal::sim
