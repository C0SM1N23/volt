#pragma once

#include <string_view>

namespace volt::metrics {

/// One name=value pair that tells metrics sharing a name apart.
///
/// Both views name storage the caller owns, and both must outlive every
/// registry the metric is added to: a label is read again on every scrape,
/// long after registration. String literals are the intended source.
struct Label final {
  std::string_view name;
  std::string_view value;

  [[nodiscard]] constexpr bool operator==(const Label &) const noexcept = default;
};

} // namespace volt::metrics
