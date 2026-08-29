#include "posix_process.hpp"

#include "posix_error.hpp"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>

namespace volt::pal::posix {

PosixProcess::~PosixProcess() {
  if (reaped_) {
    return;
  }
  // An unwaited child stays a zombie for as long as this process lives, and a
  // supervisor that restarts services would accumulate one per restart.
  static_cast<void>(::kill(identifier_, SIGKILL));
  int status = 0;
  while (::waitpid(identifier_, &status, 0) < 0 && errno == EINTR) {
    // Retry: waitpid was interrupted before it could reap the child.
  }
}

std::int32_t PosixProcess::id() const noexcept { return static_cast<std::int32_t>(identifier_); }

bool PosixProcess::running() const noexcept { return !reaped_; }

core::expected<void> PosixProcess::signal(int signal_number) const noexcept {
  if (reaped_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  if (::kill(identifier_, signal_number) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

core::expected<void> PosixProcess::request_stop() noexcept { return signal(SIGTERM); }

core::expected<void> PosixProcess::kill() noexcept { return signal(SIGKILL); }

core::expected<ProcessExit> PosixProcess::wait() noexcept {
  if (reaped_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }

  int status = 0;
  while (true) {
    const ::pid_t result = ::waitpid(identifier_, &status, 0);
    if (result == identifier_) {
      break;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return std::unexpected{detail::from_errno(errno)};
  }

  reaped_ = true;
  if (WIFSIGNALED(status)) {
    return ProcessExit{.reason = ExitReason::kSignalled,
                       .code = static_cast<std::int32_t>(WTERMSIG(status))};
  }
  return ProcessExit{.reason = ExitReason::kReturned,
                     .code = static_cast<std::int32_t>(WEXITSTATUS(status))};
}

} // namespace volt::pal::posix
