#pragma once

#include "volt/core/error_code.hpp"

namespace volt::pal::posix::detail {

/// Maps an `errno` value onto the VOLT taxonomy of SPEC 42.1.
///
/// The mapping is by what the caller can do about it, not by which call
/// failed: a caller decides between retrying, refusing to start and giving up,
/// and that is what the category tells it.
[[nodiscard]] core::ErrorCode from_errno(int error_number) noexcept;

} // namespace volt::pal::posix::detail
