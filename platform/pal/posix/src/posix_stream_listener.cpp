#include "posix_stream_listener.hpp"

#include "posix_error.hpp"
#include "posix_stream_socket.hpp"
#include "sockaddr_conversion.hpp"

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace volt::pal::posix {

PosixStreamListener::~PosixStreamListener() {
  if (!owned_path_.empty()) {
    static_cast<void>(::unlink(owned_path_.c_str()));
  }
}

core::expected<std::unique_ptr<IStreamSocket>> PosixStreamListener::accept() noexcept {
  const StreamDomain domain = owned_path_.empty() ? StreamDomain::kInet : StreamDomain::kLocal;
  while (true) {
    detail::FileDescriptor accepted{::accept4(descriptor_.get(), nullptr, nullptr, SOCK_CLOEXEC)};
    if (accepted.valid()) {
      return std::make_unique<PosixStreamSocket>(std::move(accepted), domain);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<Endpoint> PosixStreamListener::local_endpoint() const noexcept {
  // A local listener is addressed by its path, which the creator already
  // knows; there is no port to report.
  if (!owned_path_.empty()) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return detail::local_endpoint_of(descriptor_.get());
}

core::expected<void> PosixStreamListener::set_accept_timeout(core::Duration timeout) noexcept {
  // On Linux the receive timeout bounds accept() as well, so one option covers
  // both and a caller cannot end up waiting forever for a peer that never came.
  return detail::set_receive_timeout(descriptor_.get(), timeout);
}

} // namespace volt::pal::posix
