#pragma once

#include <cstddef>

namespace volt::actor {

/// Summarizes one bounded dispatcher pass.
struct DispatchReport {
  std::size_t messages = 0;
  std::size_t timers = 0;
  bool budget_exhausted = false;
};

} // namespace volt::actor
