#include "volt/sched/task_spec.hpp"

namespace volt::sched {

namespace {

/// The rules both classes share: a real name, a class that matches, and no
/// policy that would abandon a safety-critical job half done.
[[nodiscard]] core::expected<void> validate_common(const TaskSpec &spec,
                                                   SchedClass expected_class) noexcept {
  if (spec.klass != expected_class || spec.name.empty()) {
    return std::unexpected{core::ErrorCode::kConfigInvalidValue};
  }
  // A half-computed safety job is worse than a late one; SPEC 9.5 says a
  // brake job is never abandoned, so the combination is a configuration
  // mistake, caught here rather than at the first overrun.
  if (spec.criticality == Criticality::kSafetyCritical &&
      spec.on_overrun == OverrunAction::kKillJob) {
    return std::unexpected{core::ErrorCode::kConfigInvalidValue};
  }
  return {};
}

} // namespace

core::expected<void> validate_for_edf(const TaskSpec &spec) noexcept {
  const core::expected<void> common = validate_common(spec, SchedClass::kEdf);
  if (!common.has_value()) {
    return common;
  }
  const bool positive = spec.period.ns() > 0 && spec.deadline.ns() > 0 && spec.wcet_budget.ns() > 0;
  if (!positive || spec.wcet_budget.ns() > spec.deadline.ns() ||
      spec.deadline.ns() > spec.period.ns() || spec.offset.ns() < 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

core::expected<void> validate_for_rate_monotonic(const TaskSpec &spec) noexcept {
  const core::expected<void> common = validate_common(spec, SchedClass::kRateMonotonic);
  if (!common.has_value()) {
    return common;
  }
  const bool positive = spec.period.ns() > 0 && spec.deadline.ns() > 0 && spec.wcet_budget.ns() > 0;
  if (!positive || spec.wcet_budget.ns() > spec.period.ns() || spec.offset.ns() < 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

} // namespace volt::sched
