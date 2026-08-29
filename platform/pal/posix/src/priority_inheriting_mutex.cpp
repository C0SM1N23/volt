#include "volt/pal/posix/priority_inheriting_mutex.hpp"

#include <pthread.h>

namespace volt::pal::posix {

PriorityInheritingMutex::PriorityInheritingMutex() noexcept {
  ::pthread_mutexattr_t attributes{};
  if (::pthread_mutexattr_init(&attributes) != 0) {
    VOLT_ASSERT(false, "pthread_mutexattr_init failed while creating a mutex");
  }
  inherits_priority_ = ::pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT) == 0;
  if (::pthread_mutex_init(&mutex_, &attributes) != 0) {
    VOLT_ASSERT(false, "pthread_mutex_init failed while creating a mutex");
  }
  static_cast<void>(::pthread_mutexattr_destroy(&attributes));
}

PriorityInheritingMutex::~PriorityInheritingMutex() noexcept {
  static_cast<void>(::pthread_mutex_destroy(&mutex_));
}

void PriorityInheritingMutex::lock() noexcept {
  // A failure here is not a condition a caller can handle: it means the mutex
  // is destroyed, or that this thread already holds it, both of which are
  // invariant violations rather than runtime errors.
  VOLT_ASSERT(::pthread_mutex_lock(&mutex_) == 0, "pthread_mutex_lock failed");
}

bool PriorityInheritingMutex::try_lock() noexcept { return ::pthread_mutex_trylock(&mutex_) == 0; }

void PriorityInheritingMutex::unlock() noexcept {
  VOLT_ASSERT(::pthread_mutex_unlock(&mutex_) == 0, "pthread_mutex_unlock failed");
}

} // namespace volt::pal::posix
