#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"
#include "volt/pal/socket.hpp"

#include <cstddef>
#include <span>

namespace volt::pal {

/// One end of a connected byte stream.
///
/// A stream has no message boundaries: what one side sends in three calls the
/// other may read in one, or in five. Every protocol carried over it therefore
/// has to frame itself, which is why the control plane uses streams and the
/// data plane uses datagrams (SPEC 8.1).
class IStreamSocket {
public:
  IStreamSocket() = default;
  virtual ~IStreamSocket() = default;

  // Deleted because the object owns one end of a connection.
  IStreamSocket(const IStreamSocket &) = delete;
  IStreamSocket &operator=(const IStreamSocket &) = delete;
  IStreamSocket(IStreamSocket &&) = delete;
  IStreamSocket &operator=(IStreamSocket &&) = delete;

  /// Sends bytes, returning how many were accepted.
  ///
  /// Fewer than offered is normal when the peer is slow; the caller resumes
  /// from where it stopped.
  ///
  /// @pre    `payload` only has to stay alive for the call
  /// @rt     blocks when the send buffer is full; control plane only
  /// @errors kTransientPeerUnreachable when the peer closed the connection
  [[nodiscard]] virtual core::expected<std::size_t>
  send(std::span<const std::byte> payload) noexcept = 0;

  /// Reads up to `buffer.size()` bytes.
  ///
  /// @post   a zero-length result means the peer closed its end and no further
  ///         bytes will arrive; it is the end of the stream, not an error
  /// @pre    `buffer` only has to stay alive for the call
  /// @rt     blocks until bytes arrive or the receive timeout expires
  /// @errors kTransientTimeout when the receive timeout expires first
  [[nodiscard]] virtual core::expected<std::size_t>
  receive(std::span<std::byte> buffer) noexcept = 0;

  /// Closes this end for sending, so the peer reads end of stream.
  ///
  /// Half-closing rather than closing outright is what lets a caller say "I am
  /// done asking" and still read the answer.
  ///
  /// @post   later sends on this end fail; receives keep working
  [[nodiscard]] virtual core::expected<void> shutdown_send() noexcept = 0;

  /// Returns the endpoint at the other end of the connection.
  [[nodiscard]] virtual core::expected<Endpoint> peer_endpoint() const noexcept = 0;

  /// Bounds how long `receive` waits.
  ///
  /// @pre    `timeout` is positive
  /// @errors kConfigValueOutOfRange when `timeout` is zero or negative
  [[nodiscard]] virtual core::expected<void>
  set_receive_timeout(core::Duration timeout) noexcept = 0;
};

} // namespace volt::pal
