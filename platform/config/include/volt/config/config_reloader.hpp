#pragma once

#include "volt/config/config_change.hpp"
#include "volt/config/load_report.hpp"
#include "volt/config/node_config.hpp"

#include "volt/core/error.hpp"

#include <functional>
#include <string_view>

namespace volt::config {

/// Owns the immutable effective config and replaces it only after a permitted hot reload.
class ConfigReloader final {
public:
  using ChangeCallback = std::function<void(const ConfigChange &)>;

  /// Takes the initial validated config and its change callback.
  /// @pre    on_config_change remains callable for the lifetime of this object
  /// @thread control-plane configuration owner only
  /// @rt     may allocate; not for data plane
  ConfigReloader(NodeConfig initial_config, ChangeCallback on_config_change);

  /// Validates a replacement, rejects non-calibratable changes, then publishes callbacks.
  /// @pre    path remains valid only for the call; report is owned by the caller
  /// @post   success replaces current() before any later reload begins
  /// @thread control-plane configuration owner only
  /// @rt     performs file IO and allocates; not for data plane
  /// @errors configuration errors from load_node_config, or kConfigInvalidValue when a
  ///         non-calibratable field changed
  [[nodiscard]] volt::expected<void> reload(std::string_view path, LoadReport &report);

  /// Returns the current immutable effective configuration.
  /// @thread control-plane configuration owner only
  /// @rt     allocation-free; not for data plane
  [[nodiscard]] const NodeConfig &current() const noexcept;

private:
  NodeConfig current_;
  ChangeCallback on_config_change_;
};

} // namespace volt::config
