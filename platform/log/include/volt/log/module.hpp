#pragma once

#include <cstdint>
#include <string_view>

namespace volt::log {

/// Which subsystem produced a record.
///
/// Filtering is per module (SPEC 8.4), so this enum is the filter's key. The
/// values reach the binary log, so they are assigned once and never reordered;
/// a new subsystem takes the next free number.
enum class Module : std::uint8_t {
  kCore = 0,
  kPal = 1,
  kTime = 2,
  kMemory = 3,
  kIpc = 4,
  kScheduler = 5,
  kLifecycle = 6,
  kHealth = 7,
  kWatchdog = 8,
  kConfig = 9,
  kActor = 10,
  kDistributed = 11,
  kCommunication = 12,
  kDiagnostics = 13,
  kSafety = 14,
  kSecurity = 15,
  kService = 16,
  kSimulation = 17,
  kApplication = 18,
};

/// One past the highest module, which sizes the filter table.
inline constexpr std::size_t kModuleCount = 19;

/// Returns the name a decoder prints for `module`.
[[nodiscard]] constexpr std::string_view to_string(Module module) noexcept {
  switch (module) {
  case Module::kCore:
    return "core";
  case Module::kPal:
    return "pal";
  case Module::kTime:
    return "time";
  case Module::kMemory:
    return "memory";
  case Module::kIpc:
    return "ipc";
  case Module::kScheduler:
    return "sched";
  case Module::kLifecycle:
    return "lifecycle";
  case Module::kHealth:
    return "health";
  case Module::kWatchdog:
    return "watchdog";
  case Module::kConfig:
    return "config";
  case Module::kActor:
    return "actor";
  case Module::kDistributed:
    return "distributed";
  case Module::kCommunication:
    return "communication";
  case Module::kDiagnostics:
    return "diagnostics";
  case Module::kSafety:
    return "safety";
  case Module::kSecurity:
    return "security";
  case Module::kService:
    return "service";
  case Module::kSimulation:
    return "simulation";
  case Module::kApplication:
    return "application";
  }
  return "unknown";
}

} // namespace volt::log
