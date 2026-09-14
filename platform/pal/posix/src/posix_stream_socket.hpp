#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/stream_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace volt::pal::posix {

/// Which address family a stream socket belongs to.
///
/// Kept as state instead of asked from the kernel per call: the answer never
/// changes after creation, and it decides which identity questions make sense
/// (an address for TCP, credentials for a local socket).
enum class StreamDomain : std::uint8_t { kInet, kLocal };

/// One end of a TCP or local stream connection.
class PosixStreamSocket final : public IStreamSocket {
public:
  /// Adopts a connected socket. Only the platform and the listener call this.
  explicit PosixStreamSocket(detail::FileDescriptor descriptor,
                             StreamDomain domain = StreamDomain::kInet) noexcept
      : descriptor_{std::move(descriptor)}, domain_{domain} {}

  [[nodiscard]] core::expected<std::size_t>
  send(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<void> shutdown_send() noexcept override;
  [[nodiscard]] core::expected<Endpoint> peer_endpoint() const noexcept override;
  [[nodiscard]] core::expected<PeerCredentials> peer_credentials() const noexcept override;
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept override;

private:
  detail::FileDescriptor descriptor_;
  StreamDomain domain_;
};

} // namespace volt::pal::posix
