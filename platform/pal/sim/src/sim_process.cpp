#include "sim_process.hpp"

namespace volt::pal::sim {

core::expected<void> SimProcess::request_stop() noexcept {
  if (reaped_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  world_->record("process.stop", static_cast<std::uint64_t>(identifier_));
  return {};
}

core::expected<void> SimProcess::kill() noexcept {
  if (reaped_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  // A killed child no longer ends the way its program said it would, and a
  // supervisor distinguishes the two when it decides whether to restart.
  outcome_ = ProcessExit{.reason = ExitReason::kSignalled, .code = kKillSignal};
  world_->record("process.kill", static_cast<std::uint64_t>(identifier_));
  return {};
}

core::expected<ProcessExit> SimProcess::wait() noexcept {
  if (reaped_) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  reaped_ = true;
  world_->record("process.wait", static_cast<std::uint64_t>(outcome_.code));
  return outcome_;
}

} // namespace volt::pal::sim
