#pragma once

#include "volt/memory/arena.hpp"

namespace volt {

/// Uses the caller-backed bump allocator required by the actor contract.
using Allocator = memory::Arena;

} // namespace volt
