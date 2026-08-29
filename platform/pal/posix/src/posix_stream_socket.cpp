#include "posix_stream_socket.hpp"

#include "posix_error.hpp"
#include "sockaddr_conversion.hpp"

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace volt::pal::posix {

core::expected<std::size_t> PosixStreamSocket::send(std::span<const std::byte> payload) noexcept {
  while (true) {
    // MSG_NOSIGNAL: writing to a closed connection must report an error, not
    // raise SIGPIPE and kill a process that was only talking to a peer that
    // went away.
    const ::ssize_t sent = ::send(descriptor_.get(), payload.data(), payload.size(), MSG_NOSIGNAL);
    if (sent >= 0) {
      return static_cast<std::size_t>(sent);
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EPIPE || errno == ECONNRESET) {
      return std::unexpected{core::ErrorCode::kTransientPeerUnreachable};
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<std::size_t> PosixStreamSocket::receive(std::span<std::byte> buffer) noexcept {
  while (true) {
    const ::ssize_t received = ::recv(descriptor_.get(), buffer.data(), buffer.size(), 0);
    if (received >= 0) {
      // Zero means the peer shut its end down. That is the end of the stream,
      // which the caller has to tell apart from "nothing yet".
      return static_cast<std::size_t>(received);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<void> PosixStreamSocket::shutdown_send() noexcept {
  if (::shutdown(descriptor_.get(), SHUT_WR) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

core::expected<Endpoint> PosixStreamSocket::peer_endpoint() const noexcept {
  return detail::peer_endpoint_of(descriptor_.get());
}

core::expected<void> PosixStreamSocket::set_receive_timeout(core::Duration timeout) noexcept {
  return detail::set_receive_timeout(descriptor_.get(), timeout);
}

} // namespace volt::pal::posix
