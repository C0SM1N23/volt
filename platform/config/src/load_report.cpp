#include "volt/config/load_report.hpp"

#include <string>

namespace volt::config {

std::string ConfigDiagnostic::message() const {
  return file + ":" + std::to_string(line) + ": field '" + field + "': found " + found +
         "; expected " + expected;
}

const std::optional<ConfigDiagnostic> &LoadReport::diagnostic() const noexcept {
  return diagnostic_;
}

const std::vector<std::string> &LoadReport::consumed_fields() const noexcept {
  return consumed_fields_;
}

std::optional<std::size_t> LoadReport::line_for(std::string_view field) const {
  const auto position = field_lines_.find(field);
  if (position == field_lines_.end()) {
    return std::nullopt;
  }
  return position->second;
}

std::size_t LoadReport::error_count() const noexcept { return error_count_; }

} // namespace volt::config
