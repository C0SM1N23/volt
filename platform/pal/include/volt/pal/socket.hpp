#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace volt::pal {

/// An IPv4 address and port, in host byte order.
///
/// Host order throughout the API, converted at the platform boundary: a
/// program that never sees network order cannot forget to convert.
struct Endpoint {
  std::uint32_t address = 0;
  std::uint16_t port = 0;

  [[nodiscard]] constexpr bool operator==(const Endpoint &) const noexcept = default;
};

/// Outcome of a receive: how many bytes landed, and who sent them.
struct Datagram {
  std::size_t bytes = 0;
  Endpoint from;
};

/// A datagram socket.
///
/// Datagrams only, because every VOLT protocol that leaves a node is one
/// (SWIM, gPTP, SOME/IP-SD). A stream socket arrives together with the first
/// protocol that needs it, so that no interface exists without a working
/// implementation on every backend.
class ISocket {
public:
  ISocket() = default;
  virtual ~ISocket() = default;

  // Deleted because the object owns a file descriptor.
  ISocket(const ISocket &) = delete;
  ISocket &operator=(const ISocket &) = delete;
  ISocket(ISocket &&) = delete;
  ISocket &operator=(ISocket &&) = delete;

  /// Binds the socket to a local endpoint. A zero port asks for an ephemeral
  /// one, which `local_endpoint()` then reports.
  ///
  /// @pre    the socket is not bound yet
  /// @errors kResourceBusy when the endpoint is taken,
  ///         kResourceUnavailable when the address is not local
  [[nodiscard]] virtual core::expected<void> bind(Endpoint local) noexcept = 0;

  /// Sends one datagram.
  ///
  /// @pre    `payload` only has to stay alive for the call
  /// @post   the datagram was handed to the stack; delivery is not implied
  /// @rt     may block when the send buffer is full
  /// @errors kResourceExhausted when the send buffer is full,
  ///         kTransientPeerUnreachable when the stack rejects the destination
  [[nodiscard]] virtual core::expected<std::size_t> send_to(std::span<const std::byte> payload,
                                                            Endpoint destination) noexcept = 0;

  /// Receives one datagram into `buffer`.
  ///
  /// A datagram longer than `buffer` is truncated and the excess discarded,
  /// which is what the underlying stack does; the caller sizes the buffer from
  /// the protocol's maximum message.
  ///
  /// @pre    `buffer` only has to stay alive for the call
  /// @rt     blocks until a datagram arrives or the receive timeout expires
  /// @errors kTransientTimeout when the receive timeout expires first
  [[nodiscard]] virtual core::expected<Datagram>
  receive_from(std::span<std::byte> buffer) noexcept = 0;

  /// Returns the endpoint the socket is bound to.
  ///
  /// @errors kResourceUnavailable when the socket is not bound
  [[nodiscard]] virtual core::expected<Endpoint> local_endpoint() const noexcept = 0;

  /// Bounds how long `receive_from` waits, so a lost peer cannot stall a
  /// caller forever.
  ///
  /// @pre    `timeout` is positive
  /// @errors kConfigValueOutOfRange when `timeout` is zero or negative
  [[nodiscard]] virtual core::expected<void>
  set_receive_timeout(core::Duration timeout) noexcept = 0;
};

} // namespace volt::pal
