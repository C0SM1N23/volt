#pragma once

#include "volt/ipc/detail/topic_state.hpp"
#include "volt/ipc/topic.hpp"

#include <utility>

namespace volt::ipc {

/// A message slot the publisher may write, on loan from the pool
/// (SPEC 10.2): building the message happens in place, in shared memory, so
/// publishing never copies it.
///
/// Publishing consumes the loan. A loan that dies unpublished returns its
/// slot, so an error path cannot leak pool capacity.
template <typename T> class Loan final {
public:
  Loan(Loan &&other) noexcept
      : core_{std::exchange(other.core_, nullptr)}, ticket_{other.ticket_} {}
  Loan &operator=(Loan &&other) noexcept {
    if (this != &other) {
      surrender();
      core_ = std::exchange(other.core_, nullptr);
      ticket_ = other.ticket_;
    }
    return *this;
  }
  Loan(const Loan &) = delete;
  Loan &operator=(const Loan &) = delete;
  ~Loan() { surrender(); }

  [[nodiscard]] T &operator*() noexcept {
    return *detail::payload_as<T>(core_->state, ticket_.index);
  }
  [[nodiscard]] T *operator->() noexcept {
    return detail::payload_as<T>(core_->state, ticket_.index);
  }

private:
  template <typename U> friend class Publisher;

  Loan(detail::TopicCore &core, detail::LoanTicket ticket) noexcept
      : core_{&core}, ticket_{ticket} {}

  void surrender() noexcept {
    if (core_ != nullptr) {
      core_->state.cancel_loan(ticket_);
      core_ = nullptr;
    }
  }

  detail::TopicCore *core_ = nullptr;
  detail::LoanTicket ticket_{};
};

} // namespace volt::ipc
