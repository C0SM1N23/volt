#pragma once

#include "error.hpp"

#include <compare>
#include <cstdint>

namespace volt::core {

// Exact SI definitions of the sub-second units, in nanoseconds. They are not
// tunable: a different value would mean the type no longer measures time.
inline constexpr std::int64_t kNanosPerMicrosecond = 1'000;
inline constexpr std::int64_t kNanosPerMillisecond = 1'000'000;
inline constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

/// A signed span of time, held as a nanosecond count.
///
/// Nothing converts into a Duration implicitly, so a bare integer can never be
/// mistaken for a time span, and the unit is always stated at the call site.
/// Arithmetic comes in two forms: the operators, for values the caller has
/// already bounded, halt on overflow; the `checked_` functions report it as an
/// error instead, and are what untrusted input must go through.
class Duration final {
public:
  /// Constructs a zero-length span.
  constexpr Duration() noexcept = default;

  /// Builds a span from a nanosecond count.
  [[nodiscard]] static constexpr Duration from_ns(std::int64_t nanoseconds) noexcept {
    return Duration{nanoseconds};
  }

  /// Builds a span from a microsecond count.
  /// @pre the count fits in int64 nanoseconds; a violation halts
  [[nodiscard]] static constexpr Duration from_us(std::int64_t microseconds) noexcept {
    return Duration{scaled(microseconds, kNanosPerMicrosecond)};
  }

  /// Builds a span from a millisecond count.
  /// @pre the count fits in int64 nanoseconds; a violation halts
  [[nodiscard]] static constexpr Duration from_ms(std::int64_t milliseconds) noexcept {
    return Duration{scaled(milliseconds, kNanosPerMillisecond)};
  }

  /// Builds a span from a second count.
  /// @pre the count fits in int64 nanoseconds; a violation halts
  [[nodiscard]] static constexpr Duration from_s(std::int64_t seconds) noexcept {
    return Duration{scaled(seconds, kNanosPerSecond)};
  }

  /// Returns the span in nanoseconds.
  [[nodiscard]] constexpr std::int64_t ns() const noexcept { return ns_; }

  /// Adds two spans, reporting overflow rather than wrapping.
  /// @errors kInternalArithmeticOverflow when the sum leaves int64
  [[nodiscard]] constexpr expected<Duration> checked_add(Duration other) const noexcept {
    std::int64_t result = 0;
    if (__builtin_add_overflow(ns_, other.ns_, &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Duration{result};
  }

  /// Subtracts a span, reporting overflow rather than wrapping.
  /// @errors kInternalArithmeticOverflow when the difference leaves int64
  [[nodiscard]] constexpr expected<Duration> checked_sub(Duration other) const noexcept {
    std::int64_t result = 0;
    if (__builtin_sub_overflow(ns_, other.ns_, &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Duration{result};
  }

  /// Scales the span, reporting overflow rather than wrapping.
  /// @errors kInternalArithmeticOverflow when the product leaves int64
  [[nodiscard]] constexpr expected<Duration> checked_mul(std::int64_t factor) const noexcept {
    std::int64_t result = 0;
    if (__builtin_mul_overflow(ns_, factor, &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Duration{result};
  }

  /// Adds two spans.
  /// @pre the sum fits in int64 nanoseconds; a violation halts
  [[nodiscard]] friend constexpr Duration operator+(Duration lhs, Duration rhs) noexcept {
    const expected<Duration> sum = lhs.checked_add(rhs);
    VOLT_ASSERT(sum.has_value(), "duration addition left the int64 nanosecond range");
    return *sum;
  }

  /// Subtracts two spans.
  /// @pre the difference fits in int64 nanoseconds; a violation halts
  [[nodiscard]] friend constexpr Duration operator-(Duration lhs, Duration rhs) noexcept {
    const expected<Duration> difference = lhs.checked_sub(rhs);
    VOLT_ASSERT(difference.has_value(), "duration subtraction left the int64 nanosecond range");
    return *difference;
  }

  /// Reverses the direction of the span.
  /// @pre the span is not the most negative int64, which has no positive twin
  [[nodiscard]] constexpr Duration operator-() const noexcept {
    const expected<Duration> negated = Duration{}.checked_sub(*this);
    VOLT_ASSERT(negated.has_value(), "duration negation left the int64 nanosecond range");
    return *negated;
  }

  /// Orders and compares spans.
  [[nodiscard]] constexpr auto operator<=>(const Duration &) const noexcept = default;
  [[nodiscard]] constexpr bool operator==(const Duration &) const noexcept = default;

private:
  constexpr explicit Duration(std::int64_t nanoseconds) noexcept : ns_{nanoseconds} {}

  static constexpr std::int64_t scaled(std::int64_t value, std::int64_t factor) noexcept {
    std::int64_t result = 0;
    const bool overflowed = __builtin_mul_overflow(value, factor, &result);
    VOLT_ASSERT(!overflowed, "duration unit conversion left the int64 nanosecond range");
    return result;
  }

  std::int64_t ns_{};
};

} // namespace volt::core
