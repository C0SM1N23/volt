#include "posix_message_queue.hpp"

#include "posix_error.hpp"
#include "time_conversion.hpp"

#include <cerrno>
#include <ctime>
#include <optional>

namespace volt::pal::posix {

PosixMessageQueue::~PosixMessageQueue() {
  static_cast<void>(::mq_close(descriptor_));
  if (!owned_name_.empty()) {
    static_cast<void>(::mq_unlink(owned_name_.c_str()));
  }
}

core::expected<void> PosixMessageQueue::send(std::span<const std::byte> payload) noexcept {
  if (payload.size() > message_bytes_) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  // A zero absolute deadline turns the call into a try-send: the contract
  // reports a full queue instead of waiting on the consumer, and O_NONBLOCK
  // is not an option because it would also stop `receive` from waiting.
  const ::timespec immediately{};
  // The kernel's interface counts in char; std::byte was designed to alias
  // exactly this way, and the size travels alongside the pointer.
  const char *const bytes = reinterpret_cast<const char *>(payload.data());
  while (true) {
    if (::mq_timedsend(descriptor_, bytes, payload.size(), 0, &immediately) == 0) {
      return {};
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == ETIMEDOUT) {
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<std::size_t> PosixMessageQueue::receive(std::span<std::byte> buffer) noexcept {
  // The kernel refuses a buffer under the message size outright; checking
  // here names the actual mistake instead of surfacing EMSGSIZE.
  if (buffer.size() < message_bytes_) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  // The deadline is fixed before the wait so a signal interruption resumes
  // the same wait instead of quietly extending it. The kernel wants absolute
  // CLOCK_REALTIME; a wall-clock jump can stretch or shrink the wait, which
  // is acceptable for a control-plane fallback.
  std::optional<::timespec> deadline;
  if (receive_timeout_.has_value()) {
    ::timespec now{};
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0) {
      return std::unexpected{detail::from_errno(errno)};
    }
    deadline = detail::to_timespec(
        core::Duration::from_ns(detail::to_nanoseconds(now) + receive_timeout_->ns()));
  }
  // See `send` for why the pointer crosses as char.
  char *const bytes = reinterpret_cast<char *>(buffer.data());
  while (true) {
    const ::ssize_t received =
        deadline.has_value()
            ? ::mq_timedreceive(descriptor_, bytes, buffer.size(), nullptr, &*deadline)
            : ::mq_receive(descriptor_, bytes, buffer.size(), nullptr);
    if (received >= 0) {
      return static_cast<std::size_t>(received);
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == ETIMEDOUT) {
      return std::unexpected{core::ErrorCode::kTransientTimeout};
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<void> PosixMessageQueue::set_receive_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  receive_timeout_ = timeout;
  return {};
}

} // namespace volt::pal::posix
