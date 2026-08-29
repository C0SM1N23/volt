#include "volt/log/log_ring.hpp"

namespace volt::log {
namespace {

// The slot count is a power of two, so wrapping is a mask.
constexpr std::size_t kIndexMask = kSlotsPerRing - 1;
static_assert((kSlotsPerRing & kIndexMask) == 0, "the slot count has to be a power of two");

} // namespace

LogRing::LogRing() : storage_(kSlotBytes * kSlotsPerRing), lengths_(kSlotsPerRing, 0) {}

std::span<std::byte> LogRing::slot_at(std::size_t index) noexcept {
  return std::span<std::byte>{storage_}.subspan((index & kIndexMask) * kSlotBytes, kSlotBytes);
}

std::span<std::byte> LogRing::claim() noexcept {
  const std::size_t write = write_index_.load(std::memory_order_relaxed);
  // Acquire: everything the drain did to the slot it released has to be
  // visible before this thread writes over it.
  const std::size_t read = read_index_.load(std::memory_order_acquire);
  if (write - read >= kSlotsPerRing) {
    return {};
  }
  claimed_index_ = write;
  return slot_at(write);
}

void LogRing::publish(std::size_t used_bytes) noexcept {
  lengths_[claimed_index_ & kIndexMask] = static_cast<std::uint16_t>(used_bytes);
  // Release: the record bytes and the length above must be visible to the
  // drain before the index that tells it the slot is ready.
  write_index_.store(claimed_index_ + 1, std::memory_order_release);
}

void LogRing::drop() noexcept {
  // Relaxed: the counter is read on its own, and no other state depends on
  // when it becomes visible.
  dropped_.fetch_add(1, std::memory_order_relaxed);
}

std::span<const std::byte> LogRing::pop() noexcept {
  // The slot handed out by the previous call is released here rather than
  // there, which is what makes the returned span valid until the next pop. A
  // release inside the previous call would have let the producer overwrite the
  // record while the drain was still reading it.
  //
  // Release: the caller's reads of that record must complete before the
  // producer can see the slot as free.
  read_index_.store(next_to_read_, std::memory_order_release);

  // Acquire: pairs with the producer's release, so the record bytes written
  // before the index was published are visible here.
  const std::size_t write = write_index_.load(std::memory_order_acquire);
  if (next_to_read_ == write) {
    return {};
  }

  const std::size_t index = next_to_read_ & kIndexMask;
  next_to_read_ += 1;
  return slot_at(index).first(lengths_[index]);
}

std::uint64_t LogRing::dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

} // namespace volt::log
