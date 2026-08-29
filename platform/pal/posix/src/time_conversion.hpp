#pragma once

#include "volt/core/duration.hpp"

#include <ctime>

namespace volt::pal::posix::detail {

/// Splits a nanosecond span into the seconds and nanoseconds a `timespec` holds.
///
/// @pre `span` is not negative; POSIX rejects a negative timespec
[[nodiscard]] constexpr ::timespec to_timespec(core::Duration span) noexcept {
  const std::int64_t total_ns = span.ns();
  ::timespec result{};
  result.tv_sec = static_cast<::time_t>(total_ns / core::kNanosPerSecond);
  result.tv_nsec = static_cast<long>(total_ns % core::kNanosPerSecond);
  return result;
}

/// Joins the two fields of a `timespec` into a nanosecond count.
///
/// @pre the value fits in int64 nanoseconds, which holds until year 2262
[[nodiscard]] constexpr std::int64_t to_nanoseconds(const ::timespec &value) noexcept {
  return (static_cast<std::int64_t>(value.tv_sec) * core::kNanosPerSecond) +
         static_cast<std::int64_t>(value.tv_nsec);
}

} // namespace volt::pal::posix::detail
