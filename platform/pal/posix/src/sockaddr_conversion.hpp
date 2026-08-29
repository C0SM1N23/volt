#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"
#include "volt/pal/socket.hpp"

#include <netinet/in.h>
#include <sys/socket.h>

namespace volt::pal::posix::detail {

/// Converts a VOLT endpoint into the address the socket calls take.
[[nodiscard]] ::sockaddr_in to_sockaddr(Endpoint endpoint) noexcept;

/// Converts a socket address back into a VOLT endpoint.
[[nodiscard]] Endpoint to_endpoint(const ::sockaddr_in &address) noexcept;

// Every BSD socket call takes an address through the generic `sockaddr`. POSIX
// requires the family field to sit at the same offset in each address type, so
// the kernel reads it first and then interprets the rest as the matching
// concrete type. Both casts alias one live `sockaddr_in`, which outlives the
// call, and the size is always passed alongside so nothing is read past it.
// These two functions are the only place in the backend that aliases.
[[nodiscard]] const ::sockaddr *as_generic(const ::sockaddr_in &address) noexcept;
[[nodiscard]] ::sockaddr *as_generic(::sockaddr_in &address) noexcept;

/// Bounds how long a blocking read on `descriptor` waits.
///
/// Shared by the datagram and stream sockets, and by the listener: on Linux
/// the same option bounds `accept` as well.
///
/// @pre    `timeout` is positive
/// @errors kConfigValueOutOfRange when `timeout` is zero or negative
[[nodiscard]] core::expected<void> set_receive_timeout(int descriptor,
                                                       core::Duration timeout) noexcept;

/// Returns where `descriptor` is connected, for a connected socket.
[[nodiscard]] core::expected<Endpoint> peer_endpoint_of(int descriptor) noexcept;

/// Returns where `descriptor` is bound.
[[nodiscard]] core::expected<Endpoint> local_endpoint_of(int descriptor) noexcept;

} // namespace volt::pal::posix::detail
