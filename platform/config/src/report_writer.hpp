#pragma once

#include "volt/config/load_report.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace volt::config::detail {

class ReportWriter final {
public:
  explicit ReportWriter(LoadReport &report) noexcept : report_{report} {}

  void reset() {
    report_.diagnostic_.reset();
    report_.consumed_fields_.clear();
    report_.field_lines_.clear();
    report_.error_count_ = 0;
  }

  void consume(std::string field, std::size_t line) {
    report_.field_lines_.insert_or_assign(field, line);
    report_.consumed_fields_.push_back(std::move(field));
  }

  [[nodiscard]] volt::core::ErrorCode fail(ConfigDiagnostic diagnostic) {
    const volt::core::ErrorCode code = diagnostic.code;
    report_.diagnostic_ = std::move(diagnostic);
    ++report_.error_count_;
    return code;
  }

  void finish() { std::ranges::sort(report_.consumed_fields_); }

private:
  LoadReport &report_;
};

} // namespace volt::config::detail
