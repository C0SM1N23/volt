#pragma once

#include <cstdint>

namespace volt::config {

/// Selects the runtime boundary used to isolate a service.
enum class ServiceIsolation : std::uint8_t { kThread, kProcess, kPartition };

} // namespace volt::config
