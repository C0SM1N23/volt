#pragma once

#include <cstddef>

namespace volt::memory::detail {

// Bytes in the coherency line of every current VOLT deployment target
// (x86-64 and ARMv8, SPEC 3.1). Changing this requires re-running the queue
// contention benchmark because producer and consumer cursors may share a line.
inline constexpr std::size_t kCacheLineBytes = 64;

} // namespace volt::memory::detail
