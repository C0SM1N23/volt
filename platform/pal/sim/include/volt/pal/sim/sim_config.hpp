#pragma once

#include "volt/core/time.hpp"

#include <cstdint>

namespace volt::pal::sim {

/// How the simulated network treats a datagram.
///
/// The defaults deliver everything, immediately and in order. A simulation
/// only becomes adversarial when a test says so, which keeps a failure
/// attributable: an unasked-for packet loss would turn every other test into a
/// coin flip.
///
/// Reordering is not a separate knob. It is what `jitter` produces: two
/// datagrams sent in order arrive out of order exactly when the second draws a
/// smaller delay than the first, which is also how reordering happens on a
/// real network.
struct NetworkModel {
  /// Fixed one-way delay applied to every datagram.
  core::Duration latency{};

  /// Extra delay drawn uniformly from [0, jitter] per datagram.
  core::Duration jitter{};

  /// Datagrams dropped, in parts per million.
  ///
  /// Parts per million rather than a floating-point probability so the draw is
  /// integer arithmetic: a float comparison can differ in the last bit between
  /// compilers, and a simulation that reproduces only on one toolchain is not
  /// reproducible.
  std::uint32_t loss_per_million = 0;
};

/// Wall-clock origin of a simulated world, in seconds since the Unix epoch.
///
/// Roughly November 2023. The instant itself carries no meaning; what matters
/// is that it is written down rather than read from the host, so a timestamp a
/// scenario produces is the same on every run and every machine. Changing it
/// shifts every simulated wall-clock reading and nothing else.
inline constexpr std::int64_t kDefaultRealtimeOriginSeconds = 1'700'000'000;

/// Everything a simulated world needs before it starts.
///
/// The seed is a parameter and never comes from the host clock or the
/// operating system: a run that seeds itself cannot be repeated, which defeats
/// the purpose of simulating in the first place (SPEC 21.1).
struct SimConfig {
  /// Drives every random decision in the world.
  std::uint64_t seed = 0;

  /// Where the virtual monotonic clock starts.
  core::Timestamp monotonic_origin{};

  /// Distance from the virtual monotonic clock to the virtual wall clock.
  ///
  /// Explicit for the same reason as the seed: a wall clock read from the host
  /// would make two runs of the same scenario differ.
  core::Duration realtime_offset = core::Duration::from_s(kDefaultRealtimeOriginSeconds);

  /// How many bytes every file in the world may hold together, or zero for no
  /// limit.
  ///
  /// A simulated filesystem has no natural size, but the behaviour a full disk
  /// forces — stop recording, keep what is already written, count the loss
  /// (SPEC 42.3) — has to be exercised somewhere, and a real partition cannot
  /// be filled from a unit test.
  std::uint64_t file_system_capacity_bytes = 0;

  NetworkModel network{};
};

} // namespace volt::pal::sim
