#include "sockaddr_conversion.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <arpa/inet.h>
#include <cerrno>

namespace volt::pal::posix::detail {

::sockaddr_in to_sockaddr(Endpoint endpoint) noexcept {
  ::sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = ::htons(endpoint.port);
  address.sin_addr.s_addr = ::htonl(endpoint.address);
  return address;
}

Endpoint to_endpoint(const ::sockaddr_in &address) noexcept {
  return Endpoint{.address = ::ntohl(address.sin_addr.s_addr), .port = ::ntohs(address.sin_port)};
}

const ::sockaddr *as_generic(const ::sockaddr_in &address) noexcept {
  return reinterpret_cast<const ::sockaddr *>(&address);
}

::sockaddr *as_generic(::sockaddr_in &address) noexcept {
  return reinterpret_cast<::sockaddr *>(&address);
}

core::expected<void> set_receive_timeout(int descriptor, core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  const ::timespec span = to_timespec(timeout);
  ::timeval value{};
  value.tv_sec = span.tv_sec;
  // SO_RCVTIMEO counts in microseconds, so sub-microsecond precision is lost
  // here; a caller asking for less than a microsecond gets one, because zero
  // would mean "wait forever" instead of "almost no time".
  value.tv_usec = static_cast<::suseconds_t>(span.tv_nsec / core::kNanosPerMicrosecond);
  if (value.tv_sec == 0 && value.tv_usec == 0) {
    value.tv_usec = 1;
  }
  if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0) {
    return std::unexpected{from_errno(errno)};
  }
  return {};
}

core::expected<Endpoint> peer_endpoint_of(int descriptor) noexcept {
  ::sockaddr_in address{};
  ::socklen_t address_size = sizeof(address);
  if (::getpeername(descriptor, as_generic(address), &address_size) != 0) {
    return std::unexpected{from_errno(errno)};
  }
  return to_endpoint(address);
}

core::expected<Endpoint> local_endpoint_of(int descriptor) noexcept {
  ::sockaddr_in address{};
  ::socklen_t address_size = sizeof(address);
  if (::getsockname(descriptor, as_generic(address), &address_size) != 0) {
    return std::unexpected{from_errno(errno)};
  }
  return to_endpoint(address);
}

} // namespace volt::pal::posix::detail
