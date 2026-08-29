#pragma once

#include <cstdint>

namespace volt::config {

/// Classifies a service by the consequence of its failure.
enum class ServiceCriticality : std::uint8_t { kBestEffort, kMedium, kHigh, kSafetyCritical };

} // namespace volt::config
