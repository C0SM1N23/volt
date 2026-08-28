#pragma once

#include "duration.hpp"
#include "error.hpp"

#include <compare>
#include <cstdint>

namespace volt::core {

/// A point on the cluster time base, held as nanoseconds since the cluster
/// epoch (SPEC 8.2).
///
/// Deliberately not an alias of `std::chrono::time_point`: the representation
/// is fixed here because it is serialised onto the wire and compared across
/// nodes, so it must not depend on a clock's implementation-defined period.
///
/// Timestamps do not add to each other, because a sum of two points on a time
/// base has no meaning; a Timestamp shifts by a Duration, and the distance
/// between two Timestamps is a Duration.
class Timestamp final {
public:
  /// Constructs the cluster epoch itself.
  constexpr Timestamp() noexcept = default;

  /// Builds a point from its distance to the cluster epoch.
  [[nodiscard]] static constexpr Timestamp from_ns_since_epoch(std::int64_t nanoseconds) noexcept {
    return Timestamp{nanoseconds};
  }

  /// Returns the distance to the cluster epoch, in nanoseconds.
  [[nodiscard]] constexpr std::int64_t ns_since_epoch() const noexcept { return ns_; }

  /// Moves the point forward, reporting overflow rather than wrapping.
  /// @errors kInternalArithmeticOverflow when the result leaves int64
  [[nodiscard]] constexpr expected<Timestamp> checked_add(Duration offset) const noexcept {
    std::int64_t result = 0;
    if (__builtin_add_overflow(ns_, offset.ns(), &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Timestamp{result};
  }

  /// Moves the point backward, reporting overflow rather than wrapping.
  /// @errors kInternalArithmeticOverflow when the result leaves int64
  [[nodiscard]] constexpr expected<Timestamp> checked_sub(Duration offset) const noexcept {
    std::int64_t result = 0;
    if (__builtin_sub_overflow(ns_, offset.ns(), &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Timestamp{result};
  }

  /// Returns how long this point comes after `earlier`, negative if before.
  /// @errors kInternalArithmeticOverflow when the distance leaves int64
  [[nodiscard]] constexpr expected<Duration> checked_since(Timestamp earlier) const noexcept {
    std::int64_t result = 0;
    if (__builtin_sub_overflow(ns_, earlier.ns_, &result)) {
      return std::unexpected{ErrorCode::kInternalArithmeticOverflow};
    }
    return Duration::from_ns(result);
  }

  /// Orders and compares points on the same time base.
  [[nodiscard]] constexpr auto operator<=>(const Timestamp &) const noexcept = default;
  [[nodiscard]] constexpr bool operator==(const Timestamp &) const noexcept = default;

private:
  constexpr explicit Timestamp(std::int64_t nanoseconds) noexcept : ns_{nanoseconds} {}

  std::int64_t ns_{};
};

} // namespace volt::core
