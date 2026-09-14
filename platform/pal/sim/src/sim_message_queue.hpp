#pragma once

#include "sim_world.hpp"

#include "volt/pal/message_queue.hpp"

#include <optional>
#include <string>

namespace volt::pal::sim {

/// A simulated kernel message queue.
class SimMessageQueue final : public IMessageQueue {
public:
  /// @pre `world` outlives this queue; `owner` marks the creator, whose death
  ///      removes the name, as it does on the POSIX backend
  SimMessageQueue(detail::SimWorld &world, std::string name, bool owner) noexcept
      : world_{&world}, name_{std::move(name)}, owner_{owner} {}

  ~SimMessageQueue() override;

  [[nodiscard]] core::expected<void> send(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;
  [[nodiscard]] std::uint32_t depth() const noexcept override;
  [[nodiscard]] std::uint32_t message_bytes() const noexcept override;

private:
  detail::SimWorld *world_;
  std::string name_;
  bool owner_;
  std::optional<core::Duration> receive_timeout_;
};

} // namespace volt::pal::sim
