#pragma once

#include "volt/core/error.hpp"
#include "volt/ipc/topic_config.hpp"

#include <cstddef>
#include <cstdint>

namespace volt::ipc::detail {

/// Where each table of a topic's segment lives, as byte offsets from the
/// start of the mapping.
///
/// Offsets, never pointers: every process maps the segment at a different
/// address, so the layout must be expressible without one.
struct TopicLayout {
  std::uint32_t ring_capacity = 0;
  std::size_t free_head = 0;
  std::size_t free_available = 0;
  std::size_t next_table = 0;
  std::size_t state_table = 0;
  std::size_t refcount_table = 0;
  std::size_t publisher_port = 0;
  std::size_t loan_table = 0;
  std::size_t subscriber_ports = 0;
  std::size_t ring_cells = 0;
  std::size_t held_tables = 0;
  std::size_t payload = 0;
  std::size_t payload_stride = 0;
  std::size_t total_bytes = 0;
};

/// Computes the layout for a validated configuration and payload shape.
///
/// @errors kConfigValueOutOfRange when the configuration or the payload
///         alignment is unusable
[[nodiscard]] core::expected<TopicLayout> compute_layout(const TopicConfig &config,
                                                         std::size_t payload_bytes,
                                                         std::size_t payload_alignment) noexcept;

/// Digest of everything two processes must agree on before sharing a topic:
/// the layout constants, the configuration and the payload shape.
///
/// A version bump alone would not catch a payload type edited without a
/// version change; hashing the shape does.
[[nodiscard]] std::uint64_t layout_fingerprint(const TopicConfig &config, std::size_t payload_bytes,
                                               std::size_t payload_alignment,
                                               const TopicLayout &layout) noexcept;

} // namespace volt::ipc::detail
