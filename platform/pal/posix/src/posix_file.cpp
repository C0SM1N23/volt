#include "posix_file.hpp"

#include "posix_error.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace volt::pal::posix {

core::expected<std::size_t> PosixFile::read(std::span<std::byte> buffer) noexcept {
  if (mode_ != FileMode::kRead) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  while (true) {
    const ::ssize_t result = ::read(descriptor_.get(), buffer.data(), buffer.size());
    if (result >= 0) {
      return static_cast<std::size_t>(result);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<std::size_t> PosixFile::write(std::span<const std::byte> payload) noexcept {
  if (mode_ == FileMode::kRead) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  // write() is allowed to accept less than it was given, so the loop keeps
  // going until everything is placed. Returning the short count instead would
  // push this loop into every caller, and one of them would forget it.
  std::size_t written = 0;
  while (written < payload.size()) {
    const std::span<const std::byte> rest = payload.subspan(written);
    const ::ssize_t result = ::write(descriptor_.get(), rest.data(), rest.size());
    if (result > 0) {
      written += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0) {
      return std::unexpected{detail::from_errno(errno)};
    }
    // A zero-length write with a non-empty buffer means no progress is
    // possible; reporting the partial count keeps the caller honest.
    return written;
  }
  return written;
}

core::expected<void> PosixFile::flush() noexcept {
  while (true) {
    if (::fsync(descriptor_.get()) == 0) {
      return {};
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }
}

core::expected<std::uint64_t> PosixFile::size() const noexcept {
  struct ::stat status{};
  if (::fstat(descriptor_.get(), &status) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return static_cast<std::uint64_t>(status.st_size);
}

} // namespace volt::pal::posix
