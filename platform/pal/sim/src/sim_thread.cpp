#include "sim_thread.hpp"

#include "volt/core/error.hpp"

#include <utility>

namespace volt::pal::sim {

SimThread::SimThread(detail::SimWorld &world, detail::SimScheduler::Handle handle,
                     std::string name) noexcept
    : world_{&world}, handle_{handle}, name_{std::move(name)} {}

SimThread::~SimThread() { VOLT_ASSERT(joined_, "simulated thread destroyed before it was joined"); }

core::expected<void> SimThread::join() noexcept {
  if (joined_) {
    return std::unexpected{core::ErrorCode::kInternalOutOfRange};
  }
  world_->record("thread.join", handle_);
  world_->scheduler().run_until_completed(handle_);
  joined_ = true;
  return {};
}

bool SimThread::joinable() const noexcept { return !joined_; }

std::string_view SimThread::name() const noexcept { return name_; }

} // namespace volt::pal::sim
