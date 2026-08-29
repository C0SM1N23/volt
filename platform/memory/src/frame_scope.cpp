#include "volt/memory/frame_scope.hpp"

namespace volt::memory {

FrameScope::FrameScope(Arena &arena) noexcept : arena_{arena}, checkpoint_{arena.checkpoint()} {}

FrameScope::~FrameScope() noexcept { arena_.rewind(checkpoint_); }

} // namespace volt::memory
