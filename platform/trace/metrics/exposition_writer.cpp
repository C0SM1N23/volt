#include "volt/metrics/exposition_writer.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace volt::metrics {
namespace {

// Enough for the longest number this writer produces: a double in shortest
// round-trip form never exceeds twenty-four characters, and a 64-bit integer
// never exceeds twenty. Raising it costs stack in the scrape path only.
constexpr std::size_t kNumberBufferBytes = 32;

// What the Prometheus exposition format calls the values a double can take
// that have no decimal spelling.
constexpr std::string_view kPositiveInfinity = "+Inf";
constexpr std::string_view kNegativeInfinity = "-Inf";
constexpr std::string_view kNotANumber = "NaN";

} // namespace

void ExpositionWriter::append_char(char value) noexcept {
  if (written_ == out_.size()) {
    truncated_ = true;
    return;
  }
  out_[written_] = value;
  ++written_;
}

void ExpositionWriter::append(std::string_view text) noexcept {
  if (text.size() > out_.size() - written_) {
    truncated_ = true;
    return;
  }
  for (const char character : text) {
    append_char(character);
  }
}

void ExpositionWriter::append(std::uint64_t value) noexcept {
  std::array<char, kNumberBufferBytes> digits{};
  const std::to_chars_result result =
      std::to_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc{}) {
    truncated_ = true;
    return;
  }
  append(std::string_view{digits.data(), static_cast<std::size_t>(result.ptr - digits.data())});
}

void ExpositionWriter::append(double value) noexcept {
  if (std::isnan(value)) {
    append(kNotANumber);
    return;
  }
  if (std::isinf(value)) {
    append(value > 0 ? kPositiveInfinity : kNegativeInfinity);
    return;
  }

  std::array<char, kNumberBufferBytes> digits{};
  const std::to_chars_result result =
      std::to_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc{}) {
    truncated_ = true;
    return;
  }
  append(std::string_view{digits.data(), static_cast<std::size_t>(result.ptr - digits.data())});
}

void ExpositionWriter::append_help(std::string_view text) noexcept {
  for (const char character : text) {
    if (character == '\\') {
      append("\\\\");
    } else if (character == '\n') {
      append("\\n");
    } else {
      append_char(character);
    }
  }
}

void ExpositionWriter::append_label_value(std::string_view text) noexcept {
  for (const char character : text) {
    if (character == '\\') {
      append("\\\\");
    } else if (character == '\n') {
      append("\\n");
    } else if (character == '"') {
      append("\\\"");
    } else {
      append_char(character);
    }
  }
}

} // namespace volt::metrics
