#pragma once

#include <cstdint>

namespace volt {

/// Orders actor log severity without granting access to the logging backend.
enum class Level : std::uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kFatal = 5,
};

} // namespace volt
