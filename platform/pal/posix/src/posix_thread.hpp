#pragma once

#include "volt/pal/thread.hpp"

#include <pthread.h>
#include <string>
#include <string_view>

namespace volt::pal::posix {

/// A pthread created with an explicit policy, priority, affinity and name.
///
/// Nothing is inherited from the creating thread: SPEC 42.2 assigns each VOLT
/// thread its own place, and inheritance would silently give a control-plane
/// thread real-time priority just because its parent had one.
class PosixThread final : public IThread {
public:
  /// Adopts a started thread. Only the platform calls this.
  PosixThread(::pthread_t handle, std::string name) noexcept;

  /// Halts when the thread was never joined: a thread still running while its
  /// handle disappears would touch state the owner believes is gone.
  ~PosixThread() override;

  [[nodiscard]] core::expected<void> join() noexcept override;
  [[nodiscard]] bool joinable() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

private:
  ::pthread_t handle_;
  std::string name_;
  bool joined_ = false;
};

} // namespace volt::pal::posix
