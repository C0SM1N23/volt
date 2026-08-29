#pragma once

#include "arena.hpp"
#include "byte_count.hpp"

namespace volt::memory {

/// Rewinds an arena to its entry cursor when the current frame ends.
class FrameScope final {
public:
  /// Captures the arena's current bump cursor.
  /// @pre    `arena` outlives this scope
  /// @post   allocations made through `arena` belong to this frame
  /// @thread the arena's owning thread
  /// @rt     allocation-free and O(1)
  explicit FrameScope(Arena &arena) noexcept;

  /// Reclaims allocations made since construction.
  /// @post   spans allocated in this frame are invalidated
  /// @thread the arena's owning thread
  /// @rt     allocation-free and O(1)
  ~FrameScope() noexcept;

  // Rule of five because moving a scope would create two possible rewind
  // points for the same frame and make nested-frame lifetime ambiguous.
  FrameScope(const FrameScope &) = delete;
  FrameScope &operator=(const FrameScope &) = delete;
  FrameScope(FrameScope &&) = delete;
  FrameScope &operator=(FrameScope &&) = delete;

private:
  Arena &arena_;
  ByteCount checkpoint_;
};

} // namespace volt::memory
