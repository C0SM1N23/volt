#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"
#include "volt/pal/platform.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace volt::ipc {

/// Largest frame the control plane carries. Control messages are commands
/// and reports, not data; anything bigger belongs on the data plane, and the
/// bound is what lets both ends size buffers once.
inline constexpr std::size_t kMaxControlFrameBytes = std::size_t{64} * std::size_t{1024};

/// One authenticated control-plane conversation over a local stream
/// (SPEC 10.1: Unix domain sockets carry the intra-node control plane).
///
/// Frames, not bytes: each message travels as a 32-bit little-endian length
/// followed by the payload, because a stream has no boundaries of its own
/// and every control protocol needs them back.
class ControlConnection final {
public:
  /// Adopts a connected local stream whose peer is `peer`.
  ControlConnection(std::unique_ptr<pal::IStreamSocket> stream, pal::PeerCredentials peer) noexcept
      : stream_{std::move(stream)}, peer_{peer} {}

  /// Sends one frame.
  ///
  /// @pre    `frame` is at most kMaxControlFrameBytes
  /// @rt     blocks when the peer's buffer is full; control plane only
  /// @errors kInternalBufferTooSmall for an oversized frame,
  ///         kTransientPeerUnreachable when the peer closed
  [[nodiscard]] core::expected<void> send(std::span<const std::byte> frame) noexcept;

  /// Receives one whole frame into `frame`.
  ///
  /// @post   the frame is complete: a peer that closed mid-frame is an error,
  ///         not a short read
  /// @rt     blocks until a frame arrives or the receive timeout expires
  /// @errors kTransientTimeout when the timeout expires first,
  ///         kExternalNotConnected when the peer closed between frames,
  ///         kTransientPeerUnreachable when it closed inside one,
  ///         kInternalBufferTooSmall when the frame exceeds `frame`,
  ///         kTransientIntegrityCheckFailed for a length beyond the protocol
  [[nodiscard]] core::expected<std::size_t> receive(std::span<std::byte> frame) noexcept;

  /// Bounds how long `receive` waits for the next frame.
  [[nodiscard]] core::expected<void> set_receive_timeout(core::Duration timeout) noexcept {
    return stream_->set_receive_timeout(timeout);
  }

  /// The kernel-attested identity of the other end.
  [[nodiscard]] const pal::PeerCredentials &peer() const noexcept { return peer_; }

private:
  std::unique_ptr<pal::IStreamSocket> stream_;
  pal::PeerCredentials peer_;
};

/// The listening end of a control channel.
///
/// Authorization is the whole point of using local sockets here: every
/// accepted connection carries credentials the kernel filled in, and the
/// server only admits peers running as its own user. The server learns its
/// own identity at listen time by connecting to itself once - the same
/// kernel mechanism then vouches for both sides.
class ControlServer final {
public:
  /// Starts listening at `path` and learns the server's own identity.
  [[nodiscard]] static core::expected<ControlServer> listen(pal::IPlatform &platform,
                                                            std::string_view path) noexcept;

  /// Takes the next connection from a peer running as this user.
  ///
  /// @rt     blocks until a connection arrives or the accept timeout expires
  /// @errors kExternalRequestRejected when the peer runs as someone else
  ///         (the connection is closed), kTransientTimeout otherwise as usual
  [[nodiscard]] core::expected<ControlConnection> accept() noexcept;

  /// Bounds how long `accept` waits.
  [[nodiscard]] core::expected<void> set_accept_timeout(core::Duration timeout) noexcept {
    return listener_->set_accept_timeout(timeout);
  }

  /// The identity connections are checked against.
  [[nodiscard]] const pal::PeerCredentials &identity() const noexcept { return identity_; }

private:
  ControlServer(std::unique_ptr<pal::IStreamListener> listener,
                pal::PeerCredentials identity) noexcept
      : listener_{std::move(listener)}, identity_{identity} {}

  std::unique_ptr<pal::IStreamListener> listener_;
  pal::PeerCredentials identity_;
};

/// Opens a control connection to the server at `path`.
///
/// @errors kTransientPeerUnreachable when nothing listens there
[[nodiscard]] core::expected<ControlConnection> connect_control(pal::IPlatform &platform,
                                                                std::string_view path) noexcept;

} // namespace volt::ipc
