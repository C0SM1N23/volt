#pragma once

#include <cstddef>
#include <vector>

namespace volt::memory::test_support {

/// Allocates a caller-owned byte block through the installed process allocator.
///
/// Keeping this factory in a separate translation unit prevents Clang from
/// applying new-expression allocation elision to tests whose observable result
/// is the allocation counter itself.
/// @post   the returned block contains exactly `size_bytes` value-initialized bytes
/// @thread any test thread
/// @rt     may allocate; testing only
[[nodiscard]] std::vector<std::byte> allocate_block(std::size_t size_bytes);

} // namespace volt::memory::test_support
