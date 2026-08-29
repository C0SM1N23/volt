#pragma once

#include <string>

namespace volt::config {

/// Describes one accepted calibration change passed to the reload callback.
struct ConfigChange final {
  std::string field;
  std::string previous_value;
  std::string current_value;

  [[nodiscard]] bool operator==(const ConfigChange &) const noexcept = default;
};

} // namespace volt::config
