#pragma once

#include <cstdint>

namespace volt::actor {

/// Selects the bounded-mailbox action when no slot remains.
enum class MailboxFullPolicy : std::uint8_t {
  kDropOldest,
  kDropNew,
  kFault,
};

} // namespace volt::actor
