#include "posix_socket.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace volt::pal::posix {
namespace {

[[nodiscard]] ::sockaddr_in to_sockaddr(Endpoint endpoint) noexcept {
  ::sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = ::htons(endpoint.port);
  address.sin_addr.s_addr = ::htonl(endpoint.address);
  return address;
}

[[nodiscard]] Endpoint to_endpoint(const ::sockaddr_in &address) noexcept {
  return Endpoint{.address = ::ntohl(address.sin_addr.s_addr), .port = ::ntohs(address.sin_port)};
}

// Every BSD socket call takes an address through the generic `sockaddr`. POSIX
// requires the family field to sit at the same offset in each address type, so
// the kernel reads it first and then interprets the rest as the matching
// concrete type. Both casts below alias one live `sockaddr_in`, which outlives
// the call, and the size is always passed alongside so nothing is read past it.
// These two functions are the only place in the backend that aliases.
[[nodiscard]] const ::sockaddr *as_generic(const ::sockaddr_in &address) noexcept {
  return reinterpret_cast<const ::sockaddr *>(&address);
}

[[nodiscard]] ::sockaddr *as_generic(::sockaddr_in &address) noexcept {
  return reinterpret_cast<::sockaddr *>(&address);
}

} // namespace

core::expected<void> PosixSocket::bind(Endpoint local) noexcept {
  if (bound_) {
    return std::unexpected{core::ErrorCode::kResourceBusy};
  }
  const ::sockaddr_in address = to_sockaddr(local);
  if (::bind(descriptor_.get(), as_generic(address), sizeof(address)) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  bound_ = true;
  return {};
}

core::expected<std::size_t> PosixSocket::send_to(std::span<const std::byte> payload,
                                                 Endpoint destination) noexcept {
  const ::sockaddr_in address = to_sockaddr(destination);
  while (true) {
    const ::ssize_t sent = ::sendto(descriptor_.get(), payload.data(), payload.size(), 0,
                                    as_generic(address), sizeof(address));
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
                                          as_generic(address), &address_size);
    if (received >= 0) {
      return Datagram{.bytes = static_cast<std::size_t>(received), .from = to_endpoint(address)};
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
  ::sockaddr_in address{};
  ::socklen_t address_size = sizeof(address);
  if (::getsockname(descriptor_.get(), as_generic(address), &address_size) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return to_endpoint(address);
}

core::expected<void> PosixSocket::set_receive_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  const ::timespec span = detail::to_timespec(timeout);
  ::timeval value{};
  value.tv_sec = span.tv_sec;
  // SO_RCVTIMEO is expressed in microseconds, so sub-microsecond precision is
  // lost here; a caller asking for less than a microsecond gets one.
  value.tv_usec = static_cast<::suseconds_t>(span.tv_nsec / core::kNanosPerMicrosecond);
  if (value.tv_sec == 0 && value.tv_usec == 0) {
    value.tv_usec = 1;
  }
  if (::setsockopt(descriptor_.get(), SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

} // namespace volt::pal::posix
