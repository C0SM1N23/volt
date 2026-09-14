#include "volt/ipc/control_channel.hpp"

#include "volt/core/span_utils.hpp"

#include <array>

namespace volt::ipc {
namespace {

constexpr std::size_t kLengthBytes = sizeof(std::uint32_t);

/// Pushes every byte of `payload` out, resuming partial sends.
[[nodiscard]] core::expected<void> send_all(pal::IStreamSocket &stream,
                                            std::span<const std::byte> payload) noexcept {
  while (!payload.empty()) {
    const core::expected<std::size_t> sent = stream.send(payload);
    if (!sent.has_value()) {
      return std::unexpected{sent.error()};
    }
    payload = payload.subspan(*sent);
  }
  return {};
}

/// Fills `buffer` completely, or says why it could not.
///
/// A clean close before the first byte is a normal end of conversation and
/// reported as such; a close in the middle of a frame is the peer breaking
/// its word. The two must not look alike to the caller.
[[nodiscard]] core::expected<void> receive_all(pal::IStreamSocket &stream,
                                               std::span<std::byte> buffer) noexcept {
  bool any = false;
  while (!buffer.empty()) {
    const core::expected<std::size_t> received = stream.receive(buffer);
    if (!received.has_value()) {
      return std::unexpected{received.error()};
    }
    if (*received == 0) {
      return std::unexpected{any ? core::ErrorCode::kTransientPeerUnreachable
                                 : core::ErrorCode::kExternalNotConnected};
    }
    any = true;
    buffer = buffer.subspan(*received);
  }
  return {};
}

} // namespace

core::expected<void> ControlConnection::send(std::span<const std::byte> frame) noexcept {
  if (frame.size() > kMaxControlFrameBytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  std::array<std::byte, kLengthBytes> length{};
  const core::expected<void> encoded =
      core::write_little_endian<std::uint32_t>(length, 0, static_cast<std::uint32_t>(frame.size()));
  VOLT_ASSERT(encoded.has_value(), "a four-byte buffer holds a uint32");
  const core::expected<void> header_sent = send_all(*stream_, length);
  if (!header_sent.has_value()) {
    return header_sent;
  }
  return send_all(*stream_, frame);
}

core::expected<std::size_t> ControlConnection::receive(std::span<std::byte> frame) noexcept {
  std::array<std::byte, kLengthBytes> length{};
  const core::expected<void> header = receive_all(*stream_, length);
  if (!header.has_value()) {
    return std::unexpected{header.error()};
  }
  const core::expected<std::uint32_t> announced =
      core::read_little_endian<std::uint32_t>(length, 0);
  VOLT_ASSERT(announced.has_value(), "a four-byte buffer holds a uint32");
  if (*announced > kMaxControlFrameBytes) {
    // A length beyond the protocol bound is a corrupt or hostile stream, and
    // trusting it would turn one bad frame into an unbounded read.
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }
  if (*announced > frame.size()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  const core::expected<void> body = receive_all(*stream_, frame.first(*announced));
  if (!body.has_value()) {
    // Inside a frame even a clean close is a broken promise: the peer
    // announced bytes it never delivered.
    return std::unexpected{body.error() == core::ErrorCode::kExternalNotConnected
                               ? core::ErrorCode::kTransientPeerUnreachable
                               : body.error()};
  }
  return *announced;
}

core::expected<ControlServer> ControlServer::listen(pal::IPlatform &platform,
                                                    std::string_view path) noexcept {
  core::expected<std::unique_ptr<pal::IStreamListener>> listener =
      platform.listen_local(path, pal::kDefaultListenBacklog);
  if (!listener.has_value()) {
    return std::unexpected{listener.error()};
  }

  // Self-handshake: connect once to the fresh listener and read the
  // credentials the kernel attests for that connection. That is this
  // process's own identity, obtained through exactly the mechanism later
  // used to judge peers, so the two can never disagree.
  const core::expected<std::unique_ptr<pal::IStreamSocket>> probe = platform.connect_local(path);
  if (!probe.has_value()) {
    return std::unexpected{probe.error()};
  }
  const core::expected<std::unique_ptr<pal::IStreamSocket>> mirror = (*listener)->accept();
  if (!mirror.has_value()) {
    return std::unexpected{mirror.error()};
  }
  const core::expected<pal::PeerCredentials> self = (*mirror)->peer_credentials();
  if (!self.has_value()) {
    return std::unexpected{self.error()};
  }
  return ControlServer{std::move(*listener), *self};
}

core::expected<ControlConnection> ControlServer::accept() noexcept {
  core::expected<std::unique_ptr<pal::IStreamSocket>> stream = listener_->accept();
  if (!stream.has_value()) {
    return std::unexpected{stream.error()};
  }
  const core::expected<pal::PeerCredentials> peer = (*stream)->peer_credentials();
  if (!peer.has_value()) {
    return std::unexpected{peer.error()};
  }
  if (peer->user_id != identity_.user_id) {
    // Dropping the stream here closes it; the caller sees who was refused
    // in the error, and the peer sees a closed connection, not a protocol.
    return std::unexpected{core::ErrorCode::kExternalRequestRejected};
  }
  return ControlConnection{std::move(*stream), *peer};
}

core::expected<ControlConnection> connect_control(pal::IPlatform &platform,
                                                  std::string_view path) noexcept {
  core::expected<std::unique_ptr<pal::IStreamSocket>> stream = platform.connect_local(path);
  if (!stream.has_value()) {
    return std::unexpected{stream.error()};
  }
  const core::expected<pal::PeerCredentials> peer = (*stream)->peer_credentials();
  if (!peer.has_value()) {
    return std::unexpected{peer.error()};
  }
  return ControlConnection{std::move(*stream), *peer};
}

} // namespace volt::ipc
