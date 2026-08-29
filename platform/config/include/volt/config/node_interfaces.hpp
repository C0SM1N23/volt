#pragma once

#include <string>

namespace volt::config {

/// Names the operating-system interfaces assigned to one node.
struct NodeInterfaces final {
  std::string can;
  std::string ethernet;
  std::string diagnostic_ethernet;

  [[nodiscard]] bool operator==(const NodeInterfaces &) const noexcept = default;
};

} // namespace volt::config
