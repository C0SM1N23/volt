#include "volt/sched/rm_priority.hpp"

#include <algorithm>

namespace volt::sched {

core::expected<std::vector<PriorityAssignment>>
assign_rate_monotonic_priorities(std::span<const TaskSpec> specs, core::Priority floor,
                                 core::Priority ceiling) {
  if (floor.value() > ceiling.value() ||
      specs.size() > static_cast<std::size_t>(ceiling.value() - floor.value()) + 1U) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }

  std::vector<const TaskSpec *> order;
  order.reserve(specs.size());
  for (const TaskSpec &spec : specs) {
    order.push_back(&spec);
  }
  std::ranges::sort(order, [](const TaskSpec *left, const TaskSpec *right) {
    if (left->period.ns() != right->period.ns()) {
      return left->period.ns() < right->period.ns();
    }
    // Ties break toward the tighter deadline: when two tasks ask equally
    // often, the one with less slack goes first.
    if (left->deadline.ns() != right->deadline.ns()) {
      return left->deadline.ns() < right->deadline.ns();
    }
    return left->id.value() < right->id.value();
  });

  std::vector<PriorityAssignment> assignments;
  assignments.reserve(order.size());
  std::uint8_t next = ceiling.value();
  for (const TaskSpec *spec : order) {
    assignments.push_back(PriorityAssignment{.task = spec->id, .priority = core::Priority{next}});
    next = static_cast<std::uint8_t>(next - 1U);
  }
  return assignments;
}

} // namespace volt::sched
