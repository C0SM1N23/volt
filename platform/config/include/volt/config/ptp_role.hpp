#pragma once

#include <cstdint>

namespace volt::config {

/// Selects the local gPTP role.
enum class PtpRole : std::uint8_t { kAuto, kMaster, kFollower };

} // namespace volt::config
