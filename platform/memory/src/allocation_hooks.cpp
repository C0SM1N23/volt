#include "volt/memory/allocation_tracker.hpp"
#include "volt/memory/no_alloc_scope.hpp"

#include "volt/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

// The one place in VOLT that allocates. AGENTS.md 2.4 sends every allocation
// through `platform/memory`, and these are the functions the rule is talking
// about: replacing them is how the count in AGENTS.md 5.1 becomes measurable
// rather than aspirational. `ci/check_banned_patterns.sh` allows this file by
// name, and only this file (deviation DEV-008).

namespace volt::memory {
namespace {

/// The alignment the unqualified `operator new` overloads must deliver.
constexpr std::size_t kDefaultAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

/// What VOLT keeps in front of every block it hands out.
///
/// Sizes are recorded rather than asked for at release, because the unsized
/// `operator delete` is not given one and a byte total that only counts half
/// of what is released is worse than none. The owning slot travels with the
/// block for the same reason: a block is very often released by a thread other
/// than the one that allocated it.
struct BlockHeader {
  std::uint64_t user_bytes;
  /// Distance from the pointer `std::malloc` returned to the user block.
  std::uint32_t raw_offset;
  /// The tracker slot to credit at release, or kMaxTrackedThreads for none.
  std::uint16_t owner_slot;
  /// Keeps the header sixteen bytes, so an aligned user block stays aligned.
  std::uint16_t reserved;
};

// Bytes of header in front of every block. Sixteen because that is the default
// new alignment on the x86-64 and ARMv8 targets of SPEC 3.1: a header of that
// size sits below an aligned user block without disturbing its alignment.
// Growing the header past it would push every block off its alignment.
constexpr std::size_t kHeaderBytes = 16;

static_assert(sizeof(BlockHeader) == kHeaderBytes,
              "the header pads every block, so its size is a promise");
static_assert(kHeaderBytes <= kDefaultAlignment,
              "a header wider than the default alignment would misalign every block");
static_assert(kMaxTrackedThreads <= UINT16_MAX, "a slot index has to fit the header field");

/// Rounds `value` down to a multiple of `alignment`, which is a power of two.
[[nodiscard]] std::uintptr_t align_down(std::uintptr_t value, std::size_t alignment) noexcept {
  return value & ~(static_cast<std::uintptr_t>(alignment) - 1U);
}

/// Returns the header that belongs to a block handed to the caller.
///
/// The reinterpret_cast is the one AGENTS.md 2.7 asks to be justified: the
/// bytes below a user block were written as a BlockHeader by `allocate` and
/// are read back as the same type, which is exactly the object that is there.
[[nodiscard]] BlockHeader *header_of(void *user) noexcept {
  return reinterpret_cast<BlockHeader *>(static_cast<std::byte *>(user) - kHeaderBytes);
}

/// Allocates `bytes` aligned to `alignment`, or returns nullptr.
[[nodiscard]] void *allocate(std::size_t bytes, std::size_t alignment) noexcept {
  if (no_alloc_scope::active()) {
    no_alloc_scope::report_violation(bytes);
  }

  // A zero-byte request still has to come back with an address of its own, so
  // that two such blocks are distinguishable and neither is nullptr.
  const std::size_t user_bytes = bytes == 0 ? 1 : bytes;
  // At least the header's own alignment, so that the header stays addressable
  // when a caller asks for an alignment narrower than the header's fields.
  const std::size_t block_alignment =
      alignment < alignof(BlockHeader) ? alignof(BlockHeader) : alignment;

  const std::size_t padding = block_alignment + kHeaderBytes;
  if (user_bytes > SIZE_MAX - padding) {
    return nullptr;
  }

  const std::size_t raw_bytes = user_bytes + padding;
  void *const raw = std::malloc(raw_bytes);
  if (raw == nullptr) {
    return nullptr;
  }

  // The user block sits at the end of what was allocated rather than at the
  // start, so that a write past its last byte lands outside the malloc block
  // and a sanitizer still catches it. The padding ends up in front, where the
  // header lives.
  const std::uintptr_t raw_address = reinterpret_cast<std::uintptr_t>(raw);
  const std::uintptr_t user_address =
      align_down(raw_address + raw_bytes - user_bytes, block_alignment);
  // Reached by offsetting the pointer rather than by casting the address back,
  // so the result keeps the provenance of the block it points into.
  const std::size_t user_offset = static_cast<std::size_t>(user_address - raw_address);
  void *const user = static_cast<std::byte *>(raw) + user_offset;

  const std::size_t owner_slot = AllocationTracker::record_allocation(user_bytes);
  *header_of(user) =
      BlockHeader{.user_bytes = user_bytes,
                  .raw_offset = static_cast<std::uint32_t>(user_address - raw_address),
                  .owner_slot = static_cast<std::uint16_t>(owner_slot),
                  .reserved = 0};
  return user;
}

/// Releases a block that `allocate` returned. A null pointer is a no-op.
void deallocate(void *user) noexcept {
  if (user == nullptr) {
    return;
  }

  const BlockHeader header = *header_of(user);
  AllocationTracker::record_deallocation(header.user_bytes, header.owner_slot);
  std::free(static_cast<std::byte *>(user) - header.raw_offset);
}

/// Fails an allocation the way the standard requires of a throwing new.
[[noreturn]] void fail_allocation() {
#if defined(__cpp_exceptions)
  throw std::bad_alloc{};
#else
  // With exceptions off there is nothing to report failure with, and running
  // on with a null pointer would corrupt the caller instead of stopping here.
  core::assert_failed("allocation succeeded", "the heap is exhausted", __FILE__, __LINE__);
#endif
}

/// Allocates or fails, for the overloads that may not return nullptr.
[[nodiscard]] void *allocate_or_fail(std::size_t bytes, std::size_t alignment) {
  void *const memory = allocate(bytes, alignment);
  if (memory == nullptr) {
    fail_allocation();
  }
  return memory;
}

} // namespace
} // namespace volt::memory

void *operator new(std::size_t bytes) {
  return volt::memory::allocate_or_fail(bytes, volt::memory::kDefaultAlignment);
}

void *operator new[](std::size_t bytes) {
  return volt::memory::allocate_or_fail(bytes, volt::memory::kDefaultAlignment);
}

void *operator new(std::size_t bytes, std::align_val_t alignment) {
  return volt::memory::allocate_or_fail(bytes, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t bytes, std::align_val_t alignment) {
  return volt::memory::allocate_or_fail(bytes, static_cast<std::size_t>(alignment));
}

void *operator new(std::size_t bytes, [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  return volt::memory::allocate(bytes, volt::memory::kDefaultAlignment);
}

void *operator new[](std::size_t bytes, [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  return volt::memory::allocate(bytes, volt::memory::kDefaultAlignment);
}

void *operator new(std::size_t bytes, std::align_val_t alignment,
                   [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  return volt::memory::allocate(bytes, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t bytes, std::align_val_t alignment,
                     [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  return volt::memory::allocate(bytes, static_cast<std::size_t>(alignment));
}

void operator delete(void *memory) noexcept { volt::memory::deallocate(memory); }

void operator delete[](void *memory) noexcept { volt::memory::deallocate(memory); }

void operator delete(void *memory, [[maybe_unused]] std::size_t bytes) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete[](void *memory, [[maybe_unused]] std::size_t bytes) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete(void *memory, [[maybe_unused]] std::align_val_t alignment) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete[](void *memory, [[maybe_unused]] std::align_val_t alignment) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete(void *memory, [[maybe_unused]] std::size_t bytes,
                     [[maybe_unused]] std::align_val_t alignment) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete[](void *memory, [[maybe_unused]] std::size_t bytes,
                       [[maybe_unused]] std::align_val_t alignment) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete(void *memory, [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete[](void *memory, [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete(void *memory, [[maybe_unused]] std::align_val_t alignment,
                     [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  volt::memory::deallocate(memory);
}

void operator delete[](void *memory, [[maybe_unused]] std::align_val_t alignment,
                       [[maybe_unused]] const std::nothrow_t &tag) noexcept {
  volt::memory::deallocate(memory);
}
