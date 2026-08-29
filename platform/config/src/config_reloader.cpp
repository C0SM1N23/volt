#include "volt/config/config_reloader.hpp"

#include "report_writer.hpp"
#include "schema.hpp"

#include "volt/config/config_loader.hpp"

#include "volt/core/error_code.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::config {
namespace {

[[nodiscard]] std::string boolean_text(bool value) { return value ? "true" : "false"; }

[[nodiscard]] ConfigChange first_non_calibratable_change(const NodeConfig &current,
                                                         const NodeConfig &candidate) {
  if (current.node != candidate.node) {
    return ConfigChange{"node", "unchanged mapping", "changed mapping"};
  }
  if (current.cluster != candidate.cluster) {
    return ConfigChange{"cluster", "unchanged mapping", "changed mapping"};
  }
  if (current.services != candidate.services) {
    return ConfigChange{"services", "unchanged sequence", "changed sequence"};
  }
  if (current.telemetry.metrics_port != candidate.telemetry.metrics_port) {
    return ConfigChange{"telemetry.metrics_port",
                        std::to_string(current.telemetry.metrics_port.value()),
                        std::to_string(candidate.telemetry.metrics_port.value())};
  }
  return {};
}

[[nodiscard]] std::vector<ConfigChange> calibration_changes(const NodeConfig &current,
                                                            const NodeConfig &candidate) {
  std::vector<ConfigChange> changes;
  if (current.telemetry.trace_enabled != candidate.telemetry.trace_enabled) {
    changes.push_back(ConfigChange{"telemetry.trace", boolean_text(current.telemetry.trace_enabled),
                                   boolean_text(candidate.telemetry.trace_enabled)});
  }
  if (current.telemetry.recording_enabled != candidate.telemetry.recording_enabled) {
    changes.push_back(ConfigChange{"telemetry.record",
                                   boolean_text(current.telemetry.recording_enabled),
                                   boolean_text(candidate.telemetry.recording_enabled)});
  }
  return changes;
}

[[nodiscard]] bool only_calibrations_changed(const NodeConfig &current,
                                             const NodeConfig &candidate) {
  NodeConfig normalized = candidate;
  normalized.telemetry.trace_enabled = current.telemetry.trace_enabled;
  normalized.telemetry.recording_enabled = current.telemetry.recording_enabled;
  return normalized == current;
}

} // namespace

ConfigReloader::ConfigReloader(NodeConfig initial_config, ChangeCallback on_config_change)
    : current_{std::move(initial_config)}, on_config_change_{std::move(on_config_change)} {}

volt::expected<void> ConfigReloader::reload(std::string_view path, LoadReport &report) {
  auto candidate_result = load_node_config(path, report);
  if (!candidate_result.has_value()) {
    return std::unexpected{candidate_result.error()};
  }
  NodeConfig candidate = std::move(*candidate_result);
  if (!only_calibrations_changed(current_, candidate)) {
    const ConfigChange change = first_non_calibratable_change(current_, candidate);
    const std::size_t line = report.line_for(change.field).value_or(0U);
    detail::ReportWriter writer{report};
    const auto code = writer.fail(ConfigDiagnostic{
        volt::core::ErrorCode::kConfigInvalidValue, std::string{path}, line, change.field,
        "'" + change.current_value + "'", "unchanged field (calibratable: false)"});
    return std::unexpected{code};
  }
  const std::vector<ConfigChange> changes = calibration_changes(current_, candidate);
  for (const ConfigChange &change : changes) {
    VOLT_ASSERT(detail::is_calibratable(change.field),
                "reload accepted a field not marked calibratable by the schema");
  }
  current_ = std::move(candidate);
  for (const ConfigChange &change : changes) {
    on_config_change_(change);
  }
  return {};
}

const NodeConfig &ConfigReloader::current() const noexcept { return current_; }

} // namespace volt::config
