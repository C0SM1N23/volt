#pragma once

#include "volt/core/error.hpp"
#include "volt/core/time.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::pal {

/// What to create. SPEC 10.1 keeps kernel message queues as a comparison
/// point and a fallback transport, so the interface stays as small as that
/// role: fixed-size messages, a fixed depth, no priorities.
struct MessageQueueConfig {
  /// Name the queue is created under; the platform normalises it the same
  /// way it normalises shared-memory names.
  std::string_view name;
  /// How many messages may wait unread. Unprivileged processes on Linux are
  /// capped at 10 by default, and the benchmark role never needs more.
  std::uint32_t depth = 0;
  /// Size of one message. Sends beyond it are rejected, not truncated.
  std::uint32_t message_bytes = 0;
};

/// A kernel-buffered queue of fixed-size messages.
///
/// Present for the SPEC 10.1 comparison table and as a fallback when shared
/// memory is not available between two processes. The data plane never uses
/// it: every operation is a system call.
class IMessageQueue {
public:
  IMessageQueue() = default;
  virtual ~IMessageQueue() = default;

  // Deleted because the object owns a queue descriptor.
  IMessageQueue(const IMessageQueue &) = delete;
  IMessageQueue &operator=(const IMessageQueue &) = delete;
  IMessageQueue(IMessageQueue &&) = delete;
  IMessageQueue &operator=(IMessageQueue &&) = delete;

  /// Enqueues one message without waiting for space.
  ///
  /// Not waiting is the contract: a full queue reports exhaustion so the
  /// caller applies its own policy, the way every VOLT transport treats a
  /// slow consumer as the consumer's problem.
  ///
  /// @pre    `payload` fits in `message_bytes` and only lives for the call
  /// @rt     one system call; control plane only
  /// @errors kResourceExhausted when the queue is full,
  ///         kInternalBufferTooSmall when the payload exceeds the message size
  [[nodiscard]] virtual core::expected<void> send(std::span<const std::byte> payload) noexcept = 0;

  /// Dequeues the oldest message into `buffer`.
  ///
  /// @pre    `buffer` holds at least `message_bytes`, the kernel requires it
  /// @rt     blocks until a message arrives or the receive timeout expires
  /// @errors kTransientTimeout when the receive timeout expires first,
  ///         kInternalBufferTooSmall when `buffer` is under the message size
  [[nodiscard]] virtual core::expected<std::size_t>
  receive(std::span<std::byte> buffer) noexcept = 0;

  /// Bounds how long `receive` waits.
  ///
  /// @pre    `timeout` is positive
  /// @errors kConfigValueOutOfRange when `timeout` is zero or negative
  [[nodiscard]] virtual core::expected<void>
  set_receive_timeout(core::Duration timeout) noexcept = 0;

  /// Returns how many messages may wait unread.
  [[nodiscard]] virtual std::uint32_t depth() const noexcept = 0;

  /// Returns the size of one message.
  [[nodiscard]] virtual std::uint32_t message_bytes() const noexcept = 0;
};

} // namespace volt::pal
