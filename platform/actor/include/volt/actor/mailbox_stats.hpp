#pragma once

#include <cstdint>

namespace volt::actor {

/// Counts every accepted message and every full-mailbox outcome.
struct MailboxStats {
  std::uint64_t accepted = 0;
  std::uint64_t dropped_oldest = 0;
  std::uint64_t dropped_new = 0;
  std::uint64_t faults = 0;
};

} // namespace volt::actor
