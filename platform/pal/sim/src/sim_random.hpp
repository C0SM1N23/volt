#pragma once

#include <cstdint>

namespace volt::pal::sim::detail {

/// SplitMix64, the generator that seeds the xoshiro family.
///
/// Written out rather than taken from `<random>` because the standard fixes
/// the engines but not the distributions: `std::uniform_int_distribution` may
/// consume a different number of draws on another standard library, and two
/// runs of the same seed would then diverge across toolchains. This algorithm
/// is a fixed sequence of integer operations, so it produces the same stream
/// everywhere.
///
/// Source: Steele, Lea and Flood, "Fast Splittable Pseudorandom Number
/// Generators" (OOPSLA 2014), the mix used by `splitmix64`.
class SimRandom final {
public:
  /// Starts the stream at `seed`. The same seed always yields the same stream.
  constexpr explicit SimRandom(std::uint64_t seed) noexcept : state_{seed} {}

  /// Returns the next value and advances the stream.
  [[nodiscard]] constexpr std::uint64_t next() noexcept {
    state_ += kGoldenGamma;
    std::uint64_t result = state_;
    result = (result ^ (result >> kFirstShift)) * kFirstMultiplier;
    result = (result ^ (result >> kSecondShift)) * kSecondMultiplier;
    return result ^ (result >> kFinalShift);
  }

  /// Returns the next value reduced to [0, bound), or zero when `bound` is zero.
  ///
  /// Reduction is a plain modulo. It biases the low values by at most one part
  /// in 2^64 divided by the bound, which for the bounds a simulation uses
  /// (delays, parts per million) is far below anything a test can observe.
  [[nodiscard]] constexpr std::uint64_t next_below(std::uint64_t bound) noexcept {
    if (bound == 0) {
      return 0;
    }
    return next() % bound;
  }

private:
  // Constants of the published algorithm. They are the generator, not tuning:
  // changing one produces a different stream that no other implementation of
  // splitmix64 reproduces.
  static constexpr std::uint64_t kGoldenGamma = 0x9E37'79B9'7F4A'7C15ULL;
  static constexpr std::uint64_t kFirstMultiplier = 0xBF58'476D'1CE4'E5B9ULL;
  static constexpr std::uint64_t kSecondMultiplier = 0x94D0'49BB'1331'11EBULL;
  static constexpr int kFirstShift = 30;
  static constexpr int kSecondShift = 27;
  static constexpr int kFinalShift = 31;

  std::uint64_t state_;
};

} // namespace volt::pal::sim::detail
