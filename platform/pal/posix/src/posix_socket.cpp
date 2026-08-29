#include "posix_socket.hpp"

#include "posix_error.hpp"
#include "sockaddr_conversion.hpp"

#include <cerrno>
#include <sys/socket.h>

namespace volt::pal::posix {

core::expected<void> PosixSocket::bind(Endpoint local) noexcept {
  if (bound_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  const ::sockaddr_in address = detail::to_sockaddr(local);
  if (::bind(descriptor_.get(), detail::as_generic(address), sizeof(address)) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  bound_ = true;
  return {};
}

core::expected<std::size_t> PosixSocket::send_to(std::span<const std::byte> payload,
                                                 Endpoint destination) noexcept {
  const ::sockaddr_in address = detail::to_sockaddr(destination);
  while (true) {
    const ::ssize_t sent = ::sendto(descriptor_.get(), payload.data(), payload.size(), 0,
                                    detail::as_generic(address), sizeof(address));
    if (sent >= 0) {
      return static_cast<std::size_t>(sent);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<Datagram> PosixSocket::receive_from(std::span<std::byte> buffer) noexcept {
  ::sockaddr_in address{};
  ::socklen_t address_size = sizeof(address);

  while (true) {
    const ::ssize_t received = ::recvfrom(descriptor_.get(), buffer.data(), buffer.size(), 0,
                                          detail::as_generic(address), &address_size);
    if (received >= 0) {
      return Datagram{.bytes = static_cast<std::size_t>(received),
                      .from = detail::to_endpoint(address)};
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<Endpoint> PosixSocket::local_endpoint() const noexcept {
  if (!bound_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return detail::local_endpoint_of(descriptor_.get());
}

core::expected<void> PosixSocket::set_receive_timeout(core::Duration timeout) noexcept {
  return detail::set_receive_timeout(descriptor_.get(), timeout);
}

} // namespace volt::pal::posix
