#pragma once

#include "strong_id.hpp"

#include <cstdint>

namespace volt::core {

namespace detail {

// Declared and never defined: these tags exist only to separate the
// StrongId instantiations below from one another.
struct NodeIdTag;
struct ServiceIdTag;
struct InstanceIdTag;
struct TaskIdTag;
struct TopicIdTag;
struct EpochTag;
struct PriorityTag;

} // namespace detail

/// Identifies a compute node inside the cluster.
using NodeId = StrongId<detail::NodeIdTag>;

/// Identifies a service interface, independently of who runs it.
using ServiceId = StrongId<detail::ServiceIdTag>;

/// Identifies one running instance of a service.
using InstanceId = StrongId<detail::InstanceIdTag>;

/// Identifies a schedulable task.
using TaskId = StrongId<detail::TaskIdTag>;

/// Identifies a publish/subscribe topic.
using TopicId = StrongId<detail::TopicIdTag>;

/// Fencing token that orders ownership decisions (SPEC 13.3).
///
/// Held in 64 bits because a stale holder is rejected by comparing epochs: if
/// the counter ever wrapped, an old writer would compare as current again. At
/// one increment per 1 ms control cycle a 64-bit counter cannot wrap within
/// any vehicle lifetime, while a 32-bit one would wrap in about 50 days.
using Epoch = StrongId<detail::EpochTag, std::uint64_t>;

/// Scheduling priority, ordered so that a larger value is more urgent.
///
/// Held in 8 bits because the values map onto POSIX real-time priorities,
/// which SPEC 42.2 uses in the range 1..99.
using Priority = StrongId<detail::PriorityTag, std::uint8_t>;

} // namespace volt::core
