#pragma once

#include <cstdint>

namespace volt::memory {

/// What one thread, or a whole process, has asked the heap for.
///
/// A plain snapshot rather than a live view: the counters it comes from are
/// written by their owning thread while a reader is looking, so a reader is
/// handed a copy and never a moving target.
struct AllocationStats final {
  /// Allocations performed since the thread was first tracked.
  std::uint64_t allocation_count = 0;
  /// Deallocations performed since the thread was first tracked.
  std::uint64_t deallocation_count = 0;
  /// Bytes handed out across every allocation, live or already released.
  std::uint64_t total_bytes = 0;
  /// Bytes allocated and not yet released.
  std::uint64_t live_bytes = 0;
  /// Allocations made and not yet released.
  std::uint64_t live_allocations = 0;
  /// The high-water mark: the largest `live_bytes` ever reached.
  std::uint64_t peak_live_bytes = 0;
  /// The largest number of allocations live at the same time.
  std::uint64_t peak_live_allocations = 0;
  /// Allocations that happened while a `no_alloc_scope` was active.
  std::uint64_t violation_count = 0;
};

} // namespace volt::memory
