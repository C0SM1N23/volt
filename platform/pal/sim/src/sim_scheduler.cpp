#include "sim_scheduler.hpp"

#include <utility>

namespace volt::pal::sim::detail {

SimScheduler::Handle SimScheduler::add(ThreadEntry entry) {
  const Handle handle = next_handle_;
  next_handle_ += 1;
  queued_.push_back(Queued{.handle = handle, .entry = std::move(entry)});
  return handle;
}

void SimScheduler::run_until_completed(Handle handle) {
  while (last_completed_ < handle && !queued_.empty()) {
    // The body is moved out before it runs, because running it may queue more
    // work and invalidate the deque's storage.
    Queued current = std::move(queued_.front());
    queued_.pop_front();
    current.entry();
    last_completed_ = current.handle;
  }
}

} // namespace volt::pal::sim::detail
