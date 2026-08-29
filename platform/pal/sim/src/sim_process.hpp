#pragma once

#include "sim_world.hpp"

#include "volt/pal/process.hpp"

#include <cstdint>

namespace volt::pal::sim {

/// A simulated child process.
///
/// No program runs: the world is told in advance how a given executable ends,
/// and this object plays out the lifecycle around that answer. What a
/// supervisor above the PAL depends on is the lifecycle, not the arithmetic
/// the child performs, so that is what the simulation reproduces.
class SimProcess final : public IProcess {
public:
  /// @pre `world` outlives this process
  SimProcess(detail::SimWorld &world, std::int32_t identifier, ProcessExit outcome) noexcept
      : world_{&world}, identifier_{identifier}, outcome_{outcome} {}

  [[nodiscard]] std::int32_t id() const noexcept override { return identifier_; }
  [[nodiscard]] bool running() const noexcept override { return !reaped_; }
  [[nodiscard]] core::expected<void> request_stop() noexcept override;
  [[nodiscard]] core::expected<void> kill() noexcept override;
  [[nodiscard]] core::expected<ProcessExit> wait() noexcept override;

private:
  // The signal a killed child reports, matching SIGKILL on the POSIX backend
  // so a supervisor sees the same number from either platform.
  static constexpr std::int32_t kKillSignal = 9;

  detail::SimWorld *world_;
  std::int32_t identifier_;
  ProcessExit outcome_;
  bool reaped_ = false;
};

} // namespace volt::pal::sim
