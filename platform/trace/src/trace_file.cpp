#include "volt/trace/trace_file.hpp"

#include "volt/core/span_utils.hpp"

#include <array>
#include <bit>
#include <string>

namespace volt::trace {
namespace {

void append(std::vector<std::byte> &output, std::string_view text) {
  for (const char character : text) {
    output.push_back(static_cast<std::byte>(character));
  }
}

template <typename T> void append_value(std::vector<std::byte> &output, T value) {
  std::array<std::byte, sizeof(T)> encoded{};
  const core::expected<void> written = core::write_little_endian<T>(encoded, 0, value);
  VOLT_ASSERT(written.has_value(), "capture field did not fit its own size");
  output.insert(output.end(), encoded.begin(), encoded.end());
}

/// Reads one field and advances past it.
template <typename T>
[[nodiscard]] core::expected<T> take(std::span<const std::byte> content,
                                     std::size_t &offset) noexcept {
  const core::expected<T> value = core::read_little_endian<T>(content, offset);
  if (!value.has_value()) {
    return std::unexpected{value.error()};
  }
  offset += sizeof(T);
  return *value;
}

[[nodiscard]] core::expected<std::string> take_name(std::span<const std::byte> content,
                                                    std::size_t &offset) {
  const core::expected<std::uint16_t> length = take<std::uint16_t>(content, offset);
  if (!length.has_value()) {
    return std::unexpected{length.error()};
  }
  if (content.size() - offset < *length) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  std::string name;
  name.reserve(*length);
  for (std::size_t index = 0; index < *length; ++index) {
    name.push_back(static_cast<char>(std::to_integer<std::uint8_t>(content[offset + index])));
  }
  offset += *length;
  return name;
}

[[nodiscard]] core::expected<TraceRecord> take_record(std::span<const std::byte> content,
                                                      std::size_t &offset) noexcept {
  const core::expected<std::uint64_t> cycles = take<std::uint64_t>(content, offset);
  const core::expected<std::uint16_t> event = take<std::uint16_t>(content, offset);
  const core::expected<std::uint8_t> node = take<std::uint8_t>(content, offset);
  const core::expected<std::uint8_t> thread = take<std::uint8_t>(content, offset);
  const core::expected<std::uint32_t> argument = take<std::uint32_t>(content, offset);
  if (!cycles.has_value() || !event.has_value() || !node.has_value() || !thread.has_value() ||
      !argument.has_value()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  return TraceRecord{.cycles = *cycles,
                     .event = static_cast<TraceEvent>(*event),
                     .node_id = *node,
                     .thread_index = *thread,
                     .argument = *argument};
}

} // namespace

std::vector<std::byte> write_capture(const Capture &capture, const CycleClock &cycle_clock) {
  std::vector<std::byte> output;
  output.reserve(capture.records.size() * kTraceRecordBytes);

  append(output, kTraceFileMagic);
  append_value<std::uint64_t>(output, cycle_clock.origin_cycles());
  append_value<std::int64_t>(output, cycle_clock.origin_ns());
  // The rate is stored as its bit pattern so it survives the round trip
  // exactly; a decimal rendering would shift the whole axis by a rounding.
  append_value<std::uint64_t>(output,
                              std::bit_cast<std::uint64_t>(cycle_clock.ticks_per_nanosecond()));
  append_value<std::uint64_t>(output, capture.dropped);

  append_value<std::uint32_t>(output, static_cast<std::uint32_t>(capture.thread_names.size()));
  for (const std::string &name : capture.thread_names) {
    append_value<std::uint16_t>(output, static_cast<std::uint16_t>(name.size()));
    append(output, name);
  }

  append_value<std::uint32_t>(output, static_cast<std::uint32_t>(capture.records.size()));
  for (const TraceRecord &record : capture.records) {
    append_value<std::uint64_t>(output, record.cycles);
    append_value<std::uint16_t>(output, static_cast<std::uint16_t>(record.event));
    append_value<std::uint8_t>(output, record.node_id);
    append_value<std::uint8_t>(output, record.thread_index);
    append_value<std::uint32_t>(output, record.argument);
  }
  return output;
}

core::expected<StoredCapture> read_capture(std::span<const std::byte> content) {
  if (content.size() < kTraceFileMagic.size()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  // `char` may alias any object representation, which is what these bytes are.
  const std::string_view magic{reinterpret_cast<const char *>(content.data()),
                               kTraceFileMagic.size()};
  if (magic != kTraceFileMagic) {
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }

  std::size_t offset = kTraceFileMagic.size();
  const core::expected<std::uint64_t> origin_cycles = take<std::uint64_t>(content, offset);
  const core::expected<std::int64_t> origin_ns = take<std::int64_t>(content, offset);
  const core::expected<std::uint64_t> rate_bits = take<std::uint64_t>(content, offset);
  const core::expected<std::uint64_t> dropped = take<std::uint64_t>(content, offset);
  const core::expected<std::uint32_t> thread_count = take<std::uint32_t>(content, offset);
  if (!origin_cycles.has_value() || !origin_ns.has_value() || !rate_bits.has_value() ||
      !dropped.has_value() || !thread_count.has_value()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  StoredCapture stored;
  stored.capture.dropped = *dropped;
  stored.cycle_clock =
      CycleClock::from_measurement(*origin_cycles, *origin_ns, std::bit_cast<double>(*rate_bits));

  stored.capture.thread_names.reserve(*thread_count);
  for (std::uint32_t index = 0; index < *thread_count; ++index) {
    core::expected<std::string> name = take_name(content, offset);
    if (!name.has_value()) {
      return std::unexpected{name.error()};
    }
    stored.capture.thread_names.push_back(std::move(*name));
  }

  const core::expected<std::uint32_t> record_count = take<std::uint32_t>(content, offset);
  if (!record_count.has_value()) {
    return std::unexpected{record_count.error()};
  }
  stored.capture.records.reserve(*record_count);
  for (std::uint32_t index = 0; index < *record_count; ++index) {
    const core::expected<TraceRecord> record = take_record(content, offset);
    if (!record.has_value()) {
      return std::unexpected{record.error()};
    }
    stored.capture.records.push_back(*record);
  }
  return stored;
}

} // namespace volt::trace
