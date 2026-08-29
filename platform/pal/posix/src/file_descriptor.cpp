#include "file_descriptor.hpp"

#include <unistd.h>

namespace volt::pal::posix::detail {

void FileDescriptor::reset() noexcept {
  if (!valid()) {
    return;
  }
  // The result is deliberately not retried on EINTR: on Linux the descriptor
  // is released before close() can be interrupted, so retrying would close a
  // number that may already belong to another object.
  static_cast<void>(::close(descriptor_));
  descriptor_ = kNoDescriptor;
}

} // namespace volt::pal::posix::detail
