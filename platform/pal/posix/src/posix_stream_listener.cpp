#include "posix_stream_listener.hpp"

#include "posix_error.hpp"
#include "posix_stream_socket.hpp"
#include "sockaddr_conversion.hpp"

#include <cerrno>
#include <sys/socket.h>

namespace volt::pal::posix {

core::expected<std::unique_ptr<IStreamSocket>> PosixStreamListener::accept() noexcept {
  while (true) {
    detail::FileDescriptor accepted{::accept4(descriptor_.get(), nullptr, nullptr, SOCK_CLOEXEC)};
    if (accepted.valid()) {
      return std::make_unique<PosixStreamSocket>(std::move(accepted));
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<Endpoint> PosixStreamListener::local_endpoint() const noexcept {
  return detail::local_endpoint_of(descriptor_.get());
}

core::expected<void> PosixStreamListener::set_accept_timeout(core::Duration timeout) noexcept {
  // On Linux the receive timeout bounds accept() as well, so one option covers
  // both and a caller cannot end up waiting forever for a peer that never came.
  return detail::set_receive_timeout(descriptor_.get(), timeout);
}

} // namespace volt::pal::posix
