#pragma once

#include "sim_scheduler.hpp"
#include "sim_world.hpp"

#include "volt/pal/thread.hpp"

#include <string>
#include <string_view>

namespace volt::pal::sim {

/// A simulated thread.
///
/// The body does not start when the thread is created; it starts when someone
/// joins, and then runs to completion. The observable contract is the same one
/// the POSIX backend offers, which is what lets the conformance suite run over
/// both: the caller is promised that after `join()` the body has finished, and
/// nothing in the contract says when it began.
class SimThread final : public IThread {
public:
  /// @pre `world` outlives this thread
  SimThread(detail::SimWorld &world, detail::SimScheduler::Handle handle,
            std::string name) noexcept;

  /// Halts when the thread was never joined, matching the POSIX backend: a
  /// body that never ran would silently skip work the owner believes is done.
  ~SimThread() override;

  [[nodiscard]] core::expected<void> join() noexcept override;
  [[nodiscard]] bool joinable() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

private:
  detail::SimWorld *world_;
  detail::SimScheduler::Handle handle_;
  std::string name_;
  bool joined_ = false;
};

} // namespace volt::pal::sim
