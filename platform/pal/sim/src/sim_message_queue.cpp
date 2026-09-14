#include "sim_message_queue.hpp"

#include <algorithm>
#include <vector>

namespace volt::pal::sim {

SimMessageQueue::~SimMessageQueue() {
  if (owner_) {
    world_->remove_message_queue(name_);
  }
}

core::expected<void> SimMessageQueue::send(std::span<const std::byte> payload) noexcept {
  detail::SimWorld::MessageQueueState *const state = world_->find_message_queue(name_);
  if (state == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  if (payload.size() > state->message_bytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  if (state->messages.size() >= state->depth) {
    // The contract reports a full queue instead of waiting on the consumer.
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }
  state->messages.emplace_back(payload.begin(), payload.end());
  world_->record("mq.send", static_cast<std::uint64_t>(payload.size()));
  return {};
}

core::expected<std::size_t> SimMessageQueue::receive(std::span<std::byte> buffer) noexcept {
  detail::SimWorld::MessageQueueState *const state = world_->find_message_queue(name_);
  if (state == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  if (buffer.size() < state->message_bytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  if (state->messages.empty()) {
    // Nothing has arrived, and in a single-threaded world nothing will before
    // control returns here. The clock moves only when a deadline was given.
    if (receive_timeout_.has_value()) {
      world_->advance_by(receive_timeout_->ns());
    }
    world_->record("mq.timeout", static_cast<std::uint64_t>(world_->now_ns()));
    return std::unexpected{core::ErrorCode::kTransientTimeout};
  }
  const std::vector<std::byte> message = std::move(state->messages.front());
  state->messages.pop_front();
  std::copy(message.begin(), message.end(), buffer.begin());
  world_->record("mq.receive", static_cast<std::uint64_t>(message.size()));
  return message.size();
}

core::expected<void> SimMessageQueue::set_receive_timeout(core::Duration timeout) noexcept {
  if (timeout.ns() <= 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  receive_timeout_ = timeout;
  return {};
}

std::uint32_t SimMessageQueue::depth() const noexcept {
  const detail::SimWorld::MessageQueueState *const state = world_->find_message_queue(name_);
  return state == nullptr ? 0 : state->depth;
}

std::uint32_t SimMessageQueue::message_bytes() const noexcept {
  const detail::SimWorld::MessageQueueState *const state = world_->find_message_queue(name_);
  return state == nullptr ? 0 : state->message_bytes;
}

} // namespace volt::pal::sim
