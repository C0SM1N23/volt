#pragma once

#include "volt/core/error_code.hpp"

#include <cstddef>
#include <string>

namespace volt::config {

/// Carries the precise source location and schema expectation for a rejected configuration.
struct ConfigDiagnostic final {
  volt::core::ErrorCode code = volt::core::ErrorCode::kConfigInvalidValue;
  std::string file;
  std::size_t line = 0;
  std::string field;
  std::string found;
  std::string expected;

  /// Formats the diagnostic as one stable, actionable line.
  /// @thread initialization thread
  /// @rt     allocates; not for data plane
  [[nodiscard]] std::string message() const;

  [[nodiscard]] bool operator==(const ConfigDiagnostic &) const noexcept = default;
};

} // namespace volt::config
