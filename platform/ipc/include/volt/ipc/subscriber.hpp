#pragma once

#include "volt/ipc/sample.hpp"
#include "volt/ipc/topic.hpp"

#include <utility>

namespace volt::ipc {

/// One consumer seat of a topic: an SPSC ring of pending messages, depth
/// KEEP_LAST(history_depth), oldest dropped first when it falls behind.
template <typename T> class Subscriber final {
public:
  Subscriber(Subscriber &&other) noexcept
      : core_{std::exchange(other.core_, nullptr)}, port_{other.port_} {}
  Subscriber &operator=(Subscriber &&other) noexcept {
    if (this != &other) {
      leave();
      core_ = std::exchange(other.core_, nullptr);
      port_ = other.port_;
    }
    return *this;
  }
  Subscriber(const Subscriber &) = delete;
  Subscriber &operator=(const Subscriber &) = delete;

  /// Leaving releases pending and held messages and frees the seat.
  ~Subscriber() { leave(); }

  /// Takes the oldest pending message, without copying it.
  ///
  /// @rt     lock-free, no allocation, no copy
  /// @errors kResourceUnavailable when nothing is pending,
  ///         kResourceBusy when an eviction storm exhausted the retry bound;
  ///         both mean "try again", never "data lost silently"
  [[nodiscard]] core::expected<Sample<T>> take() noexcept {
    core::expected<detail::SampleTicket> ticket = core_->state.take(port_);
    if (!ticket.has_value()) {
      return std::unexpected{ticket.error()};
    }
    return Sample<T>{*core_, port_, *ticket};
  }

  /// Messages this seat lost to DROP_OLDEST since the topic was created.
  /// A growing value is the slow-consumer alarm of SPEC 12.2.
  [[nodiscard]] std::uint64_t dropped() const noexcept { return core_->state.dropped(port_); }

  /// Messages waiting to be taken.
  [[nodiscard]] std::uint64_t pending() const noexcept { return core_->state.pending(port_); }

private:
  template <typename U> friend class Topic;

  Subscriber(detail::TopicCore &core, std::uint32_t port) noexcept : core_{&core}, port_{port} {}

  void leave() noexcept {
    if (core_ != nullptr) {
      core_->state.release_subscriber(port_);
      core_ = nullptr;
    }
  }

  detail::TopicCore *core_ = nullptr;
  std::uint32_t port_ = 0;
};

template <typename T> core::expected<Subscriber<T>> Topic<T>::subscriber() noexcept {
  // A crashed subscriber must not exhaust the seats: sweep first, then claim.
  core_->recover();
  const core::expected<std::uint32_t> port =
      core_->state.claim_subscriber(core_->platform->current_process_id());
  if (!port.has_value()) {
    return std::unexpected{port.error()};
  }
  return Subscriber<T>{*core_, *port};
}

} // namespace volt::ipc
