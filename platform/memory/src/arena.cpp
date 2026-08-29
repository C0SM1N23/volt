#include "volt/memory/arena.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace volt::memory {

Arena::Arena(std::span<std::byte> storage, Alignment alignment) noexcept
    : storage_{storage}, alignment_{alignment} {}

core::expected<std::span<std::byte>> Arena::allocate(ByteCount bytes) noexcept {
  return allocate(bytes, alignment_);
}

core::expected<std::span<std::byte>> Arena::allocate(ByteCount bytes,
                                                     Alignment alignment) noexcept {
  void *candidate = storage_.data() + offset_bytes_;
  std::size_t remaining_bytes = storage_.size() - offset_bytes_;
  void *const aligned =
      std::align(alignment.bytes().bytes(), bytes.bytes(), candidate, remaining_bytes);
  if (aligned == nullptr) {
    ++allocation_failures_;
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }

  const std::span<std::byte> remaining{static_cast<std::byte *>(aligned), remaining_bytes};
  const std::span<std::byte> allocation = remaining.first(bytes.bytes());
  offset_bytes_ = storage_.size() - remaining_bytes + bytes.bytes();
  return allocation;
}

void Arena::reset() noexcept { offset_bytes_ = 0; }

ByteCount Arena::capacity_bytes() const noexcept { return ByteCount::from_bytes(storage_.size()); }

ByteCount Arena::used_bytes() const noexcept { return ByteCount::from_bytes(offset_bytes_); }

Alignment Arena::default_alignment() const noexcept { return alignment_; }

std::size_t Arena::allocation_failures() const noexcept { return allocation_failures_; }

ByteCount Arena::checkpoint() const noexcept { return used_bytes(); }

void Arena::rewind(ByteCount checkpoint) noexcept {
  VOLT_ASSERT(checkpoint.bytes() <= offset_bytes_, "arena checkpoint is beyond its bump cursor");
  offset_bytes_ = checkpoint.bytes();
}

} // namespace volt::memory
