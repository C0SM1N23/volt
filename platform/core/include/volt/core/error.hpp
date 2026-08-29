#pragma once

#include "error_code.hpp"

#include <cstddef>
#include <cstdlib>
#include <expected>

namespace volt::core {

/// Result of an operation that is allowed to fail.
///
/// Value-carrying chains compose with the monadic operations of
/// `std::expected` (`and_then`, `transform`, `or_else`); VOLT_TRY covers the
/// case where only the failure has to travel outward.
template <typename T> using expected = std::expected<T, ErrorCode>;

/// Halts the process because an internal invariant was violated.
///
/// The failure details stay in the signature so that a debugger stopped on
/// this frame shows the expression, the message and the source position. That
/// is the whole diagnostic channel available here: `platform/core` may not
/// perform IO, so nothing is written out.
///
/// @thread any
/// @rt     allocation-free; never returns
[[noreturn]] inline void assert_failed([[maybe_unused]] const char *expression,
                                       [[maybe_unused]] const char *message,
                                       [[maybe_unused]] const char *file,
                                       [[maybe_unused]] int line) noexcept {
  std::abort();
}

} // namespace volt::core

namespace volt {

/// SPEC and AGENTS.md name this alias unqualified. Both spellings denote the
/// same type, so an error result never has two incompatible forms.
template <typename T> using expected = core::expected<T>;

} // namespace volt

/// Returns from the enclosing `expected`-returning function when `expr` fails,
/// handing the caller the same ErrorCode unchanged.
#define VOLT_TRY(expr)                                                                             \
  do {                                                                                             \
    const auto volt_try_result = (expr);                                                           \
    if (!volt_try_result.has_value()) {                                                            \
      return ::std::unexpected{volt_try_result.error()};                                           \
    }                                                                                              \
  } while (false)

/// Halts when `cond` does not hold, because continuing would run on a state
/// the code has already proven impossible.
///
/// Written as one conditional expression rather than a braced statement: a
/// braced form adds two nesting levels to whatever function uses it, which
/// would push ordinary code past the nesting limit of AGENTS.md 3.11 and
/// reward removing the check.
#define VOLT_ASSERT(cond, msg)                                                                     \
  (static_cast<bool>(cond) ? static_cast<void>(0)                                                  \
                           : ::volt::core::assert_failed(#cond, (msg), __FILE__, __LINE__))

/// Makes the compile-time bound of a data-plane loop mechanically visible.
///
/// A positive bound is required because a zero-sized critical loop is dead
/// code and should be removed instead of being annotated (AGENTS.md 5.7).
#define VOLT_LOOP_BOUND(bound)                                                                     \
  static_assert(static_cast<::std::size_t>(bound) > static_cast<::std::size_t>(0),                 \
                "a critical loop must have a positive compile-time bound")
