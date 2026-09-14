#pragma once

#include "volt/ipc/loan.hpp"
#include "volt/ipc/topic.hpp"

#include <utility>

namespace volt::ipc {

/// The producer end of a topic. One per topic: every subscriber ring is
/// single-producer, which is what keeps the fan-out wait-free.
///
/// Publishing never blocks on a consumer. A consumer that lags loses its
/// oldest pending messages, counted on its own seat - the SPEC 12.2 rule
/// that no data-plane producer waits for a slow reader.
template <typename T> class Publisher final {
public:
  Publisher(Publisher &&other) noexcept : core_{std::exchange(other.core_, nullptr)} {}
  Publisher &operator=(Publisher &&other) noexcept {
    if (this != &other) {
      leave();
      core_ = std::exchange(other.core_, nullptr);
    }
    return *this;
  }
  Publisher(const Publisher &) = delete;
  Publisher &operator=(const Publisher &) = delete;

  /// Leaving cancels outstanding loans and frees the seat.
  ~Publisher() { leave(); }

  /// Borrows a slot from the pool to build one message in place.
  ///
  /// @rt     lock-free, no allocation, no copy
  /// @errors kResourceExhausted when every slot is in flight, which is the
  ///         slow-consumer backpressure surfacing at the source
  [[nodiscard]] core::expected<Loan<T>> loan() noexcept {
    core::expected<detail::LoanTicket> ticket = core_->state.loan();
    if (!ticket.has_value()) {
      return std::unexpected{ticket.error()};
    }
    return Loan<T>{*core_, *ticket};
  }

  /// Publishes a loaned message: the payload stays where it was written and
  /// only its index travels (SPEC 10.2).
  ///
  /// @rt     lock-free, no allocation, no copy
  /// @return how many subscribers received it
  std::uint32_t publish(Loan<T> &&loan) noexcept {
    VOLT_ASSERT(loan.core_ != nullptr, "publishing a loan that was already consumed");
    const detail::LoanTicket ticket = loan.ticket_;
    loan.core_ = nullptr;
    return core_->state.publish(ticket);
  }

  /// Consumers attached right now.
  [[nodiscard]] std::uint32_t subscriber_count() const noexcept {
    return core_->state.subscriber_count();
  }

private:
  template <typename U> friend class Topic;

  explicit Publisher(detail::TopicCore &core) noexcept : core_{&core} {}

  void leave() noexcept {
    if (core_ != nullptr) {
      core_->state.release_publisher();
      core_ = nullptr;
    }
  }

  detail::TopicCore *core_ = nullptr;
};

template <typename T> core::expected<Publisher<T>> Topic<T>::publisher() noexcept {
  // A crashed publisher must not brick the topic: sweep first, then claim.
  core_->recover();
  const core::expected<void> claimed =
      core_->state.claim_publisher(core_->platform->current_process_id());
  if (!claimed.has_value()) {
    return std::unexpected{claimed.error()};
  }
  return Publisher<T>{*core_};
}

} // namespace volt::ipc
