#include "volt/pal_conformance/conformance.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace volt::pal::conformance {
namespace {

/// Ties the shared suite to the Linux backend.
struct PosixBackend {
  static std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<posix::PosixPlatform>();
  }

  /// Returns a path inside a directory this test run owns, so a leftover file
  /// from an earlier run cannot make a later one pass or fail.
  static std::string writable_path(std::string_view file_name) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "volt-pal-conformance";
    std::filesystem::create_directories(directory);
    return (directory / file_name).string();
  }

  /// Programs guaranteed by POSIX to exist and to exit the way their names say.
  static std::string_view succeeding_program() { return "/bin/true"; }
  static std::string_view failing_program() { return "/bin/false"; }
  static std::string_view missing_program() { return "/volt-no-such-program"; }

  static std::string_view watchdog_path() { return "/dev/watchdog"; }

  /// The device is absent on a container and needs privileges on a desktop, so
  /// the watchdog tests only run where one is actually reachable.
  static bool provides_watchdog() {
    posix::PosixPlatform platform;
    return platform.open_watchdog(watchdog_path()).has_value();
  }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Posix, PalConformance, PosixBackend);

} // namespace
} // namespace volt::pal::conformance
