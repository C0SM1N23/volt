#include "posix_thread.hpp"

#include "volt/core/error.hpp"

#include <pthread.h>
#include <utility>

namespace volt::pal::posix {

PosixThread::PosixThread(::pthread_t handle, std::string name) noexcept
    : handle_{handle}, name_{std::move(name)} {}

PosixThread::~PosixThread() {
  VOLT_ASSERT(joined_, "thread handle destroyed before the thread was joined");
}

core::expected<void> PosixThread::join() noexcept {
  if (joined_) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  const int result = ::pthread_join(handle_, nullptr);
  if (result != 0) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  joined_ = true;
  return {};
}

bool PosixThread::joinable() const noexcept { return !joined_; }

std::string_view PosixThread::name() const noexcept { return name_; }

} // namespace volt::pal::posix
