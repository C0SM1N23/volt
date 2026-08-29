#pragma once

#include "sim_network.hpp"
#include "sim_world.hpp"

#include "volt/pal/stream_listener.hpp"

#include <memory>
#include <optional>

namespace volt::pal::sim {

/// A simulated listening socket.
class SimStreamListener final : public IStreamListener {
public:
  /// @pre `world` outlives this listener
  SimStreamListener(detail::SimWorld &world, detail::SimNetwork::SocketId listener) noexcept
      : world_{&world}, listener_{listener} {}

  [[nodiscard]] core::expected<std::unique_ptr<IStreamSocket>> accept() noexcept override;
  [[nodiscard]] core::expected<Endpoint> local_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_accept_timeout(core::Duration timeout) noexcept override;

private:
  detail::SimWorld *world_;
  detail::SimNetwork::SocketId listener_;
  std::optional<core::Duration> accept_timeout_;
};

} // namespace volt::pal::sim
