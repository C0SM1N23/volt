#pragma once

#include "volt/core/error.hpp"

#include <pthread.h>

namespace volt::pal::posix {

/// A mutex that lends its priority to whoever holds it.
///
/// SPEC 5.3 forbids plain mutexes anywhere a real-time thread can reach: with
/// the default protocol, a low-priority holder can be preempted by an unrelated
/// medium-priority thread while a real-time waiter is blocked behind it, and
/// the deadline is missed for a reason invisible in the code. With priority
/// inheritance the holder is raised to the waiter's priority until it releases.
///
/// It satisfies the standard Lockable requirements, so it works with
/// `std::scoped_lock` and friends.
class PriorityInheritingMutex final {
public:
  /// Creates an unlocked mutex.
  ///
  /// @post the mutex uses the priority-inheritance protocol
  PriorityInheritingMutex() noexcept;

  // Rule of five because the object owns a pthread mutex, which has to be
  // destroyed exactly once and must not move while a waiter is queued on it.
  ~PriorityInheritingMutex() noexcept;
  PriorityInheritingMutex(const PriorityInheritingMutex &) = delete;
  PriorityInheritingMutex &operator=(const PriorityInheritingMutex &) = delete;
  PriorityInheritingMutex(PriorityInheritingMutex &&) = delete;
  PriorityInheritingMutex &operator=(PriorityInheritingMutex &&) = delete;

  /// Blocks until the mutex is held by this thread.
  ///
  /// @pre    the calling thread does not already hold it
  /// @thread any
  /// @rt     blocks; never on the 1 ms path
  void lock() noexcept;

  /// Takes the mutex if it is free.
  ///
  /// @post   returns true only when the caller now holds it
  [[nodiscard]] bool try_lock() noexcept;

  /// Releases the mutex.
  ///
  /// @pre the calling thread holds it
  void unlock() noexcept;

  /// Reports whether the priority-inheritance protocol was accepted.
  ///
  /// A platform that refuses it leaves a mutex that still locks correctly but
  /// no longer protects against priority inversion, which callers on the
  /// real-time path must treat as a startup failure rather than ignore.
  [[nodiscard]] bool inherits_priority() const noexcept { return inherits_priority_; }

private:
  ::pthread_mutex_t mutex_{};
  bool inherits_priority_ = false;
};

} // namespace volt::pal::posix
