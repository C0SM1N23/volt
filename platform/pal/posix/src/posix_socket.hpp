#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/socket.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace volt::pal::posix {

/// A UDP socket over IPv4.
class PosixSocket final : public ISocket {
public:
  /// Adopts an already created socket. Only the platform calls this.
  explicit PosixSocket(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  [[nodiscard]] core::expected<void> bind(Endpoint local) noexcept override;
  [[nodiscard]] core::expected<std::size_t> send_to(std::span<const std::byte> payload,
                                                    Endpoint destination) noexcept override;
  [[nodiscard]] core::expected<Datagram>
  receive_from(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<Endpoint> local_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;

private:
  detail::FileDescriptor descriptor_;
  bool bound_ = false;
};

} // namespace volt::pal::posix
