#pragma once

#include "volt/pal/thread.hpp"

#include <cstdint>
#include <deque>

namespace volt::pal::sim::detail {

/// Runs simulated threads one at a time, in the order they were created.
///
/// A simulated world has one real thread, so a body runs to completion once it
/// starts. That is a deliberate restriction rather than a shortcut: with no
/// preemption there are no interleavings to choose from, so a scenario replays
/// identically without the scheduler having to record anything.
///
/// A body may create further threads; they queue behind whatever is already
/// waiting, so creation order stays the execution order.
class SimScheduler final {
public:
  using Handle = std::uint64_t;

  /// Queues `entry` and returns the handle that identifies it.
  [[nodiscard]] Handle add(ThreadEntry entry);

  /// Runs queued bodies until the one behind `handle` has finished.
  ///
  /// @pre  `handle` came from `add` on this scheduler
  /// @post every body queued no later than `handle` has run to completion
  void run_until_completed(Handle handle);

  /// Reports whether the body behind `handle` has already run.
  [[nodiscard]] bool completed(Handle handle) const noexcept { return handle <= last_completed_; }

private:
  struct Queued {
    Handle handle;
    ThreadEntry entry;
  };

  std::deque<Queued> queued_;
  // Handles are handed out in increasing order and bodies run in that same
  // order, so one number says which of them are done.
  Handle last_completed_ = 0;
  Handle next_handle_ = 1;
};

} // namespace volt::pal::sim::detail
