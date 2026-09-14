#pragma once

#include "volt/core/error.hpp"

#include <cstdint>

namespace volt::ipc {

/// Shape of one topic's shared segment, fixed at creation.
///
/// The QoS of SPEC 12.2 appears here in its transport-level form: the history
/// is KEEP_LAST(`history_depth`) and the on-full policy is DROP_OLDEST with
/// the loss counted per subscriber. Everything else in that table concerns
/// the middleware above this layer.
struct TopicConfig {
  /// How many messages may exist at once: loans held by the publisher plus
  /// samples anywhere between publish and release.
  std::uint32_t slot_count = 0;
  /// How many published messages a subscriber that has not caught up may
  /// still read; the oldest beyond it is dropped and counted.
  std::uint32_t history_depth = 0;
  /// How many subscribers may attach at once.
  std::uint32_t max_subscribers = 0;
};

/// Bounds that keep every per-topic table small enough to walk in recovery
/// without unbounded work, generous enough for any real topic in SPEC 3.2.
inline constexpr std::uint32_t kMaxSlotCount = 4096;
inline constexpr std::uint32_t kMaxHistoryDepth = 1024;
inline constexpr std::uint32_t kMaxSubscribers = 32;

/// Rejects a configuration the layout could not honour.
///
/// @errors kConfigValueOutOfRange for a zero or above-bound field, or a
///         history deeper than the slot pool that would guarantee drops of
///         data nobody has seen
[[nodiscard]] constexpr core::expected<void> validate(const TopicConfig &config) noexcept {
  const bool in_range = config.slot_count > 0 && config.slot_count <= kMaxSlotCount &&
                        config.history_depth > 0 && config.history_depth <= kMaxHistoryDepth &&
                        config.max_subscribers > 0 && config.max_subscribers <= kMaxSubscribers;
  if (!in_range || config.history_depth > config.slot_count) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

} // namespace volt::ipc
