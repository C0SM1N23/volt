#pragma once

#include "volt/ipc/detail/topic_state.hpp"
#include "volt/ipc/topic.hpp"

#include <utility>

namespace volt::ipc {

/// A published message as a subscriber sees it: a read-only view into shared
/// memory, alive for exactly as long as someone holds a reference
/// (SPEC 10.2). The slot returns to the pool when the last holder lets go.
template <typename T> class Sample final {
public:
  Sample(Sample &&other) noexcept
      : core_{std::exchange(other.core_, nullptr)}, port_{other.port_}, ticket_{other.ticket_} {}
  Sample &operator=(Sample &&other) noexcept {
    if (this != &other) {
      release();
      core_ = std::exchange(other.core_, nullptr);
      port_ = other.port_;
      ticket_ = other.ticket_;
    }
    return *this;
  }
  Sample(const Sample &) = delete;
  Sample &operator=(const Sample &) = delete;
  ~Sample() { release(); }

  [[nodiscard]] const T &operator*() const noexcept {
    return *detail::payload_as<const T>(core_->state, ticket_.index);
  }
  [[nodiscard]] const T *operator->() const noexcept {
    return detail::payload_as<const T>(core_->state, ticket_.index);
  }

private:
  template <typename U> friend class Subscriber;

  Sample(detail::TopicCore &core, std::uint32_t port, detail::SampleTicket ticket) noexcept
      : core_{&core}, port_{port}, ticket_{ticket} {}

  void release() noexcept {
    if (core_ != nullptr) {
      core_->state.release_sample(port_, ticket_);
      core_ = nullptr;
    }
  }

  detail::TopicCore *core_ = nullptr;
  std::uint32_t port_ = 0;
  detail::SampleTicket ticket_{};
};

} // namespace volt::ipc
