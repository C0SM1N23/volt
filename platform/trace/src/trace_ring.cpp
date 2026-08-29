#include "volt/trace/trace_ring.hpp"

namespace volt::trace {
namespace {

constexpr std::size_t kIndexMask = kRecordsPerRing - 1;
static_assert((kRecordsPerRing & kIndexMask) == 0, "the record count has to be a power of two");

} // namespace

TraceRing::TraceRing() : storage_(kRecordsPerRing) {}

void TraceRing::push(const TraceRecord &record) noexcept {
  const std::size_t write = write_index_.load(std::memory_order_relaxed);
  // Acquire: the collector's read of a slot must complete before this thread
  // writes over it.
  const std::size_t read = read_index_.load(std::memory_order_acquire);
  if (write - read >= kRecordsPerRing) {
    // Relaxed: the counter carries nothing else with it.
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  storage_[write & kIndexMask] = record;
  // Release: the record above must be visible to the collector before the
  // index that tells it the slot is ready.
  write_index_.store(write + 1, std::memory_order_release);
}

bool TraceRing::pop(TraceRecord &record) noexcept {
  const std::size_t read = read_index_.load(std::memory_order_relaxed);
  // Acquire: pairs with the producer's release, so the record written before
  // the index was published is visible here.
  const std::size_t write = write_index_.load(std::memory_order_acquire);
  if (read == write) {
    return false;
  }

  // Copied out rather than handed back by reference, because the slot becomes
  // the producer's again the moment the index below is published.
  record = storage_[read & kIndexMask];
  // Release: the copy above must complete before the producer can see the slot
  // as free.
  read_index_.store(read + 1, std::memory_order_release);
  return true;
}

std::uint64_t TraceRing::dropped() const noexcept {
  return dropped_.load(std::memory_order_relaxed);
}

} // namespace volt::trace
