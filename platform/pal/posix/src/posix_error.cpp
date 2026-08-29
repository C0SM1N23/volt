#include "posix_error.hpp"

#include <cerrno>

namespace volt::pal::posix::detail {

core::ErrorCode from_errno(int error_number) noexcept {
  switch (error_number) {
  case EADDRINUSE:
  case EBUSY:
    return core::ErrorCode::kResourceBusy;

  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
  case EWOULDBLOCK:
#endif
  case ETIMEDOUT:
    return core::ErrorCode::kTransientTimeout;

  case ECONNREFUSED:
  case EHOSTUNREACH:
  case ENETUNREACH:
  case ENETDOWN:
    return core::ErrorCode::kTransientPeerUnreachable;

  case EMFILE:
  case ENFILE:
  case ENOMEM:
  case ENOBUFS:
  case ENOSPC:
    return core::ErrorCode::kResourceExhausted;

  case EINVAL:
  case EDOM:
  case ERANGE:
  case ENAMETOOLONG:
    return core::ErrorCode::kConfigValueOutOfRange;

  case EFAULT:
  case EOVERFLOW:
    return core::ErrorCode::kInternalOutOfRange;

  default:
    // Everything left over means the facility is not there for us: absent
    // (ENOENT), refused (EACCES, EPERM) or not supported (ENOTTY, ENOSYS).
    return core::ErrorCode::kResourceUnavailable;
  }
}

} // namespace volt::pal::posix::detail
