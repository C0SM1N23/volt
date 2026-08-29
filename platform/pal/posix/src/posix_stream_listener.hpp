#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/stream_listener.hpp"

#include <memory>
#include <utility>

namespace volt::pal::posix {

/// A listening TCP socket.
class PosixStreamListener final : public IStreamListener {
public:
  /// Adopts a socket that is already bound and listening.
  explicit PosixStreamListener(detail::FileDescriptor descriptor) noexcept
      : descriptor_{std::move(descriptor)} {}

  [[nodiscard]] core::expected<std::unique_ptr<IStreamSocket>> accept() noexcept override;
  [[nodiscard]] core::expected<Endpoint> local_endpoint() const noexcept override;
  [[nodiscard]] core::expected<void> set_accept_timeout(core::Duration timeout) noexcept override;

private:
  detail::FileDescriptor descriptor_;
};

} // namespace volt::pal::posix
