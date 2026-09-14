#pragma once

#include "volt/core/time.hpp"
#include "volt/pal/message_queue.hpp"

#include <cstdint>
#include <mqueue.h>
#include <optional>
#include <string>

namespace volt::pal::posix {

/// A POSIX message queue.
class PosixMessageQueue final : public IMessageQueue {
public:
  /// Adopts an open queue. `owned_name` is empty for an opener; the creator
  /// passes the kernel name so the queue is removed when it dies.
  PosixMessageQueue(::mqd_t descriptor, std::string owned_name, std::uint32_t depth,
                    std::uint32_t message_bytes) noexcept
      : descriptor_{descriptor}, owned_name_{std::move(owned_name)}, depth_{depth},
        message_bytes_{message_bytes} {}

  ~PosixMessageQueue() override;

  [[nodiscard]] core::expected<void> send(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;
  [[nodiscard]] std::uint32_t depth() const noexcept override { return depth_; }
  [[nodiscard]] std::uint32_t message_bytes() const noexcept override { return message_bytes_; }

private:
  ::mqd_t descriptor_;
  std::string owned_name_;
  std::uint32_t depth_;
  std::uint32_t message_bytes_;
  std::optional<core::Duration> receive_timeout_;
};

} // namespace volt::pal::posix
