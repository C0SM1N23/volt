#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/stream_socket.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace volt::pal::posix {

/// One end of a TCP connection.
class PosixStreamSocket final : public IStreamSocket {
public:
  /// Adopts a connected socket. Only the platform and the listener call this.
  explicit PosixStreamSocket(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  [[nodiscard]] core::expected<std::size_t>
  send(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<void> shutdown_send() noexcept override;
  [[nodiscard]] core::expected<Endpoint> peer_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;

private:
  detail::FileDescriptor descriptor_;
};

} // namespace volt::pal::posix
