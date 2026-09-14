#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/stream_listener.hpp"

#include <memory>
#include <string>
#include <utility>

namespace volt::pal::posix {

/// A listening TCP or local socket.
class PosixStreamListener final : public IStreamListener {
public:
  /// Adopts a socket that is already bound and listening.
  explicit PosixStreamListener(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  /// Adopts a listening local socket that owns the filesystem name it is
  /// bound to. The name is removed when the listener dies, so an orderly
  /// shutdown leaves no corpse for the next listener to clean up.
  PosixStreamListener(detail::FileDescriptor descriptor, std::string owned_path) noexcept
      : descriptor_{std::move(descriptor)}, owned_path_{std::move(owned_path)} {}

  ~PosixStreamListener() override;

  [[nodiscard]] core::expected<std::unique_ptr<IStreamSocket>> accept() noexcept override;
  [[nodiscard]] core::expected<Endpoint> local_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_accept_timeout(core::Duration timeout) noexcept override;

private:
  detail::FileDescriptor descriptor_;
  /// Empty for TCP; the bound filesystem path for a local listener.
  std::string owned_path_;
};

} // namespace volt::pal::posix
