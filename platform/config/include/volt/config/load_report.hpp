#pragma once

#include "volt/config/config_diagnostic.hpp"

#include "volt/core/error_code.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace volt::config {

namespace detail {
class ReportWriter;
}

/// Records one load attempt's diagnostic, consumed fields, source lines, and error count.
class LoadReport final {
public:
  /// Constructs an empty report.
  LoadReport() = default;

  /// Returns the diagnostic from the failed attempt, if one occurred.
  /// @thread initialization thread
  /// @rt     allocation-free; not for data plane
  [[nodiscard]] const std::optional<ConfigDiagnostic> &diagnostic() const noexcept;

  /// Returns every YAML field accepted by the schema during the successful attempt.
  /// @thread initialization thread
  /// @rt     allocation-free; not for data plane
  [[nodiscard]] const std::vector<std::string> &consumed_fields() const noexcept;

  /// Returns the one-based source line for an accepted field.
  /// @thread initialization thread
  /// @rt     bounded lookup; not for data plane
  [[nodiscard]] std::optional<std::size_t> line_for(std::string_view field) const;

  /// Returns how many validation failures this report has observed.
  /// @thread initialization thread
  /// @rt     allocation-free; not for data plane
  [[nodiscard]] std::size_t error_count() const noexcept;

private:
  friend class detail::ReportWriter;

  std::optional<ConfigDiagnostic> diagnostic_;
  std::vector<std::string> consumed_fields_;
  std::map<std::string, std::size_t, std::less<>> field_lines_;
  std::size_t error_count_ = 0;
};

} // namespace volt::config
