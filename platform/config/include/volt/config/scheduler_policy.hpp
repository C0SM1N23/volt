#pragma once

#include <cstdint>

namespace volt::config {

/// Selects the scheduler algorithm requested by a service.
enum class SchedulerPolicy : std::uint8_t { kFifo };

} // namespace volt::config
