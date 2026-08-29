#pragma once

#include "volt/core/error.hpp"
#include "volt/log/argument_type.hpp"
#include "volt/log/level.hpp"
#include "volt/log/module.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace volt::log {

/// One decoded argument.
using Argument = std::variant<std::int64_t, std::uint64_t, double, bool, std::string_view>;

/// Reads a record back out of the bytes a RecordWriter produced.
///
/// Every step is bounds checked against the buffer it was handed, because a
/// decoder runs over files that may have been truncated by a full disk or a
/// power cut, and it has to say so rather than read past the end.
class RecordReader final {
public:
  /// @pre `record` outlives this reader; text arguments point into it
  explicit RecordReader(std::span<const std::byte> record) noexcept : record_{record} {}

  /// Reads the fixed part.
  ///
  /// @errors kInternalBufferTooSmall when the buffer is shorter than a header,
  ///         kTransientIntegrityCheckFailed when the length field disagrees
  ///         with the buffer
  [[nodiscard]] core::expected<void> parse_header() noexcept;

  [[nodiscard]] std::uint64_t format_id() const noexcept { return format_id_; }
  [[nodiscard]] std::int64_t timestamp_ns() const noexcept { return timestamp_ns_; }
  [[nodiscard]] Level level() const noexcept { return level_; }
  [[nodiscard]] Module module() const noexcept { return module_; }
  [[nodiscard]] std::size_t argument_count() const noexcept { return argument_count_; }
  [[nodiscard]] std::size_t total_bytes() const noexcept { return total_bytes_; }

  /// Reads the next argument.
  ///
  /// @errors kInternalBufferTooSmall when the record ends mid-argument,
  ///         kTransientIntegrityCheckFailed for an unknown type tag
  [[nodiscard]] core::expected<Argument> next_argument() noexcept;

private:
  std::span<const std::byte> record_;
  std::size_t offset_ = 0;
  std::uint64_t format_id_ = 0;
  std::int64_t timestamp_ns_ = 0;
  std::size_t total_bytes_ = 0;
  std::size_t argument_count_ = 0;
  Level level_ = Level::kTrace;
  Module module_ = Module::kCore;
};

} // namespace volt::log
