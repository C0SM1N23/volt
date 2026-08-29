#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::metrics {

/// Appends text to a caller-owned buffer, remembering if it ran out.
///
/// A span rather than a string, because a scrape runs on the control-plane
/// thread of a system that would rather report a truncated page than allocate
/// one under load. Overflow is recorded and every later append is dropped, so
/// a caller checks once at the end instead of at every step.
///
/// @thread one writer
/// @rt     allocation-free
class ExpositionWriter final {
public:
  /// Writes into `out`, which the caller owns and must keep alive.
  explicit ExpositionWriter(std::span<char> out) noexcept : out_{out} {}

  /// Appends `text` verbatim.
  void append(std::string_view text) noexcept;

  /// Appends `value` in decimal.
  void append(std::uint64_t value) noexcept;

  /// Appends `value` in the shortest form that reads back identically.
  ///
  /// Infinities and not-a-number are spelled the way the Prometheus exposition
  /// format spells them, because `inf` from the standard library is not a
  /// token that format accepts.
  void append(double value) noexcept;

  /// Appends `text` with the escapes a `# HELP` line requires.
  ///
  /// A backslash and a newline are the only two characters that can end a help
  /// line early; a quote is ordinary text there.
  void append_help(std::string_view text) noexcept;

  /// Appends `text` with the escapes a label value requires.
  ///
  /// A quote as well, since a label value is delimited by quotes.
  void append_label_value(std::string_view text) noexcept;

  /// Returns how many bytes were written.
  [[nodiscard]] std::size_t written() const noexcept { return written_; }

  /// Returns what was written so far.
  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view{out_.data(), written_};
  }

  /// Reports whether anything had to be dropped for want of room.
  [[nodiscard]] bool truncated() const noexcept { return truncated_; }

private:
  /// Appends one character, or records that the buffer is full.
  void append_char(char value) noexcept;

  std::span<char> out_;
  std::size_t written_ = 0;
  bool truncated_ = false;
};

} // namespace volt::metrics
