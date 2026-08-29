#include "volt/memory/allocation_tracker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace volt::memory {
namespace {

/// Raises `peak` to `candidate` when the owning thread has set a new record.
///
/// Only the owning thread raises a peak, so a plain load and store is enough.
/// A reader that catches the pair half-done reads the older of two values
/// that were both true peaks.
void raise_peak(std::atomic<std::uint64_t> &peak, std::uint64_t candidate) noexcept {
  if (candidate > peak.load(std::memory_order_relaxed)) {
    peak.store(candidate, std::memory_order_relaxed);
  }
}

/// Allocates once and reports whether the tracker noticed.
[[nodiscard]] bool probe_for_hooks() noexcept {
  const std::uint64_t before = AllocationTracker::current_thread_stats().allocation_count;
  {
    // A unique_ptr rather than a bare allocation, because AGENTS.md 2.4 keeps
    // the allocation operators to the one file that defines them. One byte is
    // enough: the question is whether the counter moves, not by how much.
    const std::unique_ptr<std::byte> probe = std::make_unique<std::byte>();
    // Written through a volatile so the store is an effect the compiler has to
    // perform. [expr.new] lets an implementation omit an allocation whose
    // result it can prove goes nowhere, and an optimised build does exactly
    // that, which would leave this probe measuring nothing.
    const std::byte *volatile escaped = probe.get();
    if (escaped == nullptr) {
      return false;
    }
  }
  return AllocationTracker::current_thread_stats().allocation_count > before;
}

} // namespace

bool AllocationTracker::hooks_installed() noexcept {
  // Probed once: the answer is fixed for the life of the program, and the
  // callers who need it are reporting paths that should not allocate at all.
  static const bool installed = probe_for_hooks();
  return installed;
}

AllocationTracker &AllocationTracker::instance() noexcept {
  // Function-local static: built on first use, which may well be the first
  // allocation of the process, and destroyed after every thread that counts.
  static AllocationTracker tracker;
  return tracker;
}

std::size_t AllocationTracker::slot_of_current_thread() noexcept {
  // initial-exec rather than the default TLS model, for the reason the tracer
  // gives: with the default, reading a thread-local calls into the dynamic
  // loader, and this one is read on every single allocation.
  [[gnu::tls_model("initial-exec")]] static thread_local std::size_t slot = kUnclaimedSlot;
  if (slot != kUnclaimedSlot) {
    return slot;
  }

  AllocationTracker &tracker = instance();
  // fetch_add rather than a load and a store: threads register concurrently
  // and each needs a slot no other thread took.
  const std::size_t index = tracker.claimed_slots_.fetch_add(1, std::memory_order_relaxed);
  if (index >= kMaxTrackedThreads) {
    tracker.threads_refused_.fetch_add(1, std::memory_order_relaxed);
    slot = kMaxTrackedThreads;
    return slot;
  }

  slot = index;
  return slot;
}

std::size_t AllocationTracker::record_allocation(std::size_t bytes) noexcept {
  const std::size_t slot = slot_of_current_thread();
  if (slot >= kMaxTrackedThreads) {
    return kMaxTrackedThreads;
  }

  ThreadCounters &counters = instance().threads_[slot];
  counters.allocation_count.fetch_add(1, std::memory_order_relaxed);
  counters.total_bytes.fetch_add(bytes, std::memory_order_relaxed);
  // fetch_add returns the value from before the addition, so the live totals
  // below are the ones this allocation produced. Re-reading the counter
  // instead would report a value another thread's release had already lowered.
  const std::uint64_t live_bytes =
      counters.live_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
  const std::uint64_t live_allocations =
      counters.live_allocations.fetch_add(1, std::memory_order_relaxed) + 1;
  raise_peak(counters.peak_live_bytes, live_bytes);
  raise_peak(counters.peak_live_allocations, live_allocations);

  return slot;
}

void AllocationTracker::record_deallocation(std::size_t bytes, std::size_t owner_index) noexcept {
  if (owner_index >= kMaxTrackedThreads) {
    return;
  }

  ThreadCounters &counters = instance().threads_[owner_index];
  counters.deallocation_count.fetch_add(1, std::memory_order_relaxed);
  counters.live_bytes.fetch_sub(bytes, std::memory_order_relaxed);
  counters.live_allocations.fetch_sub(1, std::memory_order_relaxed);
}

void AllocationTracker::record_violation() noexcept {
  const std::size_t slot = slot_of_current_thread();
  if (slot >= kMaxTrackedThreads) {
    return;
  }
  instance().threads_[slot].violation_count.fetch_add(1, std::memory_order_relaxed);
}

AllocationStats AllocationTracker::current_thread_stats() noexcept {
  const std::size_t slot = slot_of_current_thread();
  if (slot >= kMaxTrackedThreads) {
    return AllocationStats{};
  }
  return instance().stats_of_slot(slot);
}

AllocationStats AllocationTracker::stats_of_slot(std::size_t index) const noexcept {
  const ThreadCounters &counters = threads_[index];
  return AllocationStats{
      .allocation_count = counters.allocation_count.load(std::memory_order_relaxed),
      .deallocation_count = counters.deallocation_count.load(std::memory_order_relaxed),
      .total_bytes = counters.total_bytes.load(std::memory_order_relaxed),
      .live_bytes = counters.live_bytes.load(std::memory_order_relaxed),
      .live_allocations = counters.live_allocations.load(std::memory_order_relaxed),
      .peak_live_bytes = counters.peak_live_bytes.load(std::memory_order_relaxed),
      .peak_live_allocations = counters.peak_live_allocations.load(std::memory_order_relaxed),
      .violation_count = counters.violation_count.load(std::memory_order_relaxed)};
}

AllocationStats AllocationTracker::process_stats() const noexcept {
  AllocationStats total;
  const std::size_t slots = tracked_threads();
  for (std::size_t index = 0; index < slots; ++index) {
    const AllocationStats thread = stats_of_slot(index);
    total.allocation_count += thread.allocation_count;
    total.deallocation_count += thread.deallocation_count;
    total.total_bytes += thread.total_bytes;
    total.live_bytes += thread.live_bytes;
    total.live_allocations += thread.live_allocations;
    // Summed rather than maximised: the peaks of two threads are both real at
    // once, so their sum bounds what the process could have held. It is an
    // upper bound, not an instant anybody observed.
    total.peak_live_bytes += thread.peak_live_bytes;
    total.peak_live_allocations += thread.peak_live_allocations;
    total.violation_count += thread.violation_count;
  }
  return total;
}

std::size_t AllocationTracker::tracked_threads() const noexcept {
  // Claims keep counting past the last slot so that a refusal stays visible,
  // so the number of slots in use is the claim count capped at the registry.
  return std::min(claimed_slots_.load(std::memory_order_relaxed), kMaxTrackedThreads);
}

std::uint64_t AllocationTracker::threads_refused() const noexcept {
  return threads_refused_.load(std::memory_order_relaxed);
}

} // namespace volt::memory
