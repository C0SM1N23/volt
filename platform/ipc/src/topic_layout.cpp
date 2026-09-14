#include "volt/ipc/detail/topic_layout.hpp"

#include "volt/core/hash.hpp"
#include "volt/ipc/detail/topic_state.hpp"

#include <array>
#include <bit>
#include <span>

namespace volt::ipc::detail {
namespace {

/// Cache line of every target in SPEC 3.1. Tables whose two sides are
/// written by different parties start on separate lines so ownership traffic
/// does not oscillate one line between two cores.
constexpr std::size_t kTableAlignment = 64;

[[nodiscard]] constexpr std::size_t align_up(std::size_t offset, std::size_t alignment) noexcept {
  return (offset + alignment - 1U) & ~(alignment - 1U);
}

/// Walks the segment forward, one aligned table at a time.
class Cursor final {
public:
  /// Reserves a table of `count` elements of `element_bytes` and returns
  /// where it starts.
  [[nodiscard]] std::size_t table(std::size_t count, std::size_t element_bytes) noexcept {
    offset_ = align_up(offset_, kTableAlignment);
    const std::size_t start = offset_;
    offset_ += count * element_bytes;
    return start;
  }

  [[nodiscard]] std::size_t end() const noexcept { return offset_; }

private:
  std::size_t offset_ = 0;
};

} // namespace

core::expected<TopicLayout> compute_layout(const TopicConfig &config, std::size_t payload_bytes,
                                           std::size_t payload_alignment) noexcept {
  const core::expected<void> valid = validate(config);
  if (!valid.has_value()) {
    return std::unexpected{valid.error()};
  }
  if (payload_bytes == 0 || payload_alignment == 0 || payload_alignment > kTableAlignment ||
      !std::has_single_bit(payload_alignment)) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }

  const std::size_t slots = config.slot_count;
  const std::size_t subscribers = config.max_subscribers;

  TopicLayout layout{};
  // The mask trick in the ring wants a power of two; the QoS depth stays
  // exact and is enforced separately against the cursors.
  layout.ring_capacity = std::bit_ceil(config.history_depth);
  layout.payload_stride = align_up(payload_bytes, payload_alignment);

  Cursor cursor;
  static_cast<void>(cursor.table(1, sizeof(SegmentHeader)));
  // One table of two words: the head and the availability gauge share a
  // line on purpose, every party that touches one touches both.
  layout.free_head = cursor.table(2, sizeof(std::uint64_t));
  layout.free_available = layout.free_head + sizeof(std::uint64_t);
  layout.next_table = cursor.table(slots, sizeof(std::uint32_t));
  layout.state_table = cursor.table(slots, sizeof(std::uint32_t));
  layout.refcount_table = cursor.table(slots, sizeof(std::uint32_t));
  layout.publisher_port = cursor.table(1, sizeof(PublisherPort));
  layout.loan_table = cursor.table(slots, sizeof(std::uint32_t));
  layout.subscriber_ports = cursor.table(subscribers, sizeof(SubscriberPort));
  layout.ring_cells = cursor.table(subscribers * layout.ring_capacity, sizeof(std::uint32_t));
  layout.held_tables = cursor.table(subscribers * slots, sizeof(std::uint32_t));
  layout.payload = cursor.table(slots, layout.payload_stride);
  layout.total_bytes = cursor.end();
  return layout;
}

std::uint64_t layout_fingerprint(const TopicConfig &config, std::size_t payload_bytes,
                                 std::size_t payload_alignment,
                                 const TopicLayout &layout) noexcept {
  const std::array<std::uint64_t, 12> canonical{
      kSegmentVersion,    config.slot_count,     config.history_depth,  config.max_subscribers,
      payload_bytes,      payload_alignment,     layout.ring_capacity,  layout.payload_stride,
      layout.total_bytes, sizeof(SegmentHeader), sizeof(PublisherPort), sizeof(SubscriberPort)};
  return core::xxhash64(std::as_bytes(std::span{canonical}));
}

} // namespace volt::ipc::detail
