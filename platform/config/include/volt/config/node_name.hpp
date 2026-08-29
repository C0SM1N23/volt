#pragma once

#include <compare>
#include <string>
#include <string_view>

namespace volt::config {

/// Identifies a configured compute node without allowing an arbitrary string in its place.
class NodeName final {
public:
  /// Constructs an empty name for aggregate initialization before validated loading.
  NodeName() = default;

  /// Copies a validated node name.
  /// @pre    name is non-empty and remains valid only for the duration of the call
  /// @thread initialization thread
  /// @rt     allocates; not for data plane
  explicit NodeName(std::string_view name) : value_{name} {}

  /// Returns the configured spelling.
  /// @thread any, while the owning configuration remains alive
  /// @rt     allocation-free
  [[nodiscard]] std::string_view value() const noexcept { return value_; }

  /// Orders and compares node names.
  [[nodiscard]] auto operator<=>(const NodeName &) const noexcept = default;
  [[nodiscard]] bool operator==(const NodeName &) const noexcept = default;

private:
  std::string value_;
};

} // namespace volt::config
