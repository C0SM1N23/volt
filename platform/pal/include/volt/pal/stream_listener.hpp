#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"
#include "volt/pal/socket.hpp"
#include "volt/pal/stream_socket.hpp"

#include <memory>

namespace volt::pal {

/// How many connections may wait to be accepted before the platform refuses
/// further ones.
///
/// The control plane has a handful of peers, so a small queue is enough; a
/// deeper one would only hide that the acceptor has stopped keeping up.
inline constexpr unsigned kDefaultListenBacklog = 16;

/// A socket that accepts incoming connections.
class IStreamListener {
public:
  IStreamListener() = default;
  virtual ~IStreamListener() = default;

  IStreamListener(const IStreamListener &) = delete;
  IStreamListener &operator=(const IStreamListener &) = delete;
  IStreamListener(IStreamListener &&) = delete;
  IStreamListener &operator=(IStreamListener &&) = delete;

  /// Takes the next established connection.
  ///
  /// A connection is established as soon as the peer connects, before anyone
  /// accepts it, so a caller that connects and then accepts on one thread
  /// makes progress.
  ///
  /// @rt     blocks until a connection is waiting or the accept timeout expires
  /// @errors kTransientTimeout when the accept timeout expires first
  [[nodiscard]] virtual core::expected<std::unique_ptr<IStreamSocket>> accept() noexcept = 0;

  /// Returns the endpoint the listener is bound to.
  [[nodiscard]] virtual core::expected<Endpoint> local_endpoint() const noexcept = 0;

  /// Bounds how long `accept` waits.
  ///
  /// @pre    `timeout` is positive
  /// @errors kConfigValueOutOfRange when `timeout` is zero or negative
  [[nodiscard]] virtual core::expected<void>
  set_accept_timeout(core::Duration timeout) noexcept = 0;
};

} // namespace volt::pal
