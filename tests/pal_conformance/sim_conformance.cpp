#include "volt/pal_conformance/conformance.hpp"

#include "volt/pal/sim/sim_platform.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace volt::pal::conformance {
namespace {

/// Ties the shared suite to the deterministic backend.
///
/// The world knows nothing about the host, so everything the suite needs to
/// find — programs to start, a watchdog device — is declared here instead of
/// discovered. That is the whole point: a simulated run depends on what the
/// scenario says, never on the machine it runs on.
struct SimBackend {
  // Any fixed value. It is written down rather than generated so a failure in
  // the shared suite reproduces exactly.
  static constexpr std::uint64_t kSeed = 0x0110'C0DE'5EED'0001ULL;

  static constexpr std::string_view kWatchdogPath = "/sim/watchdog";

  static std::unique_ptr<IPlatform> create_platform() {
    auto platform = std::make_unique<sim::SimPlatform>(sim::SimConfig{.seed = kSeed});
    platform->register_program(succeeding_program(),
                               ProcessExit{.reason = ExitReason::kReturned, .code = 0});
    platform->register_program(failing_program(),
                               ProcessExit{.reason = ExitReason::kReturned, .code = 1});
    platform->set_watchdog_path(kWatchdogPath);
    return platform;
  }

  /// Any path works: the world's files exist because something wrote them.
  static std::string writable_path(std::string_view file_name) {
    return std::string{"/sim/"} + std::string{file_name};
  }

  static std::string_view succeeding_program() { return "/sim/true"; }
  static std::string_view failing_program() { return "/sim/false"; }
  static std::string_view missing_program() { return "/sim/never-registered"; }

  static std::string_view watchdog_path() { return kWatchdogPath; }

  /// Always: a simulated device needs no hardware and no privilege, so the two
  /// watchdog cases the POSIX run has to skip are actually exercised here.
  static bool provides_watchdog() { return true; }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Sim, PalConformance, SimBackend);

} // namespace
} // namespace volt::pal::conformance
