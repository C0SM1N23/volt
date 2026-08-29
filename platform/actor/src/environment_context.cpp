#include "volt/actor/environment_context.hpp"

#include <limits>

namespace volt::actor {
namespace {

// Constants from Steele, Lea and Flood, "Fast Splittable Pseudorandom Number
// Generators" (OOPSLA 2014). They define SplitMix64's bit-exact stream, so
// changing any one would make recorded actor inputs replay differently.
constexpr std::uint64_t kGoldenGamma = 0x9E37'79B9'7F4A'7C15ULL;
constexpr std::uint64_t kFirstMultiplier = 0xBF58'476D'1CE4'E5B9ULL;
constexpr std::uint64_t kSecondMultiplier = 0x94D0'49BB'1331'11EBULL;
constexpr int kFirstShift = 30;
constexpr int kSecondShift = 27;
constexpr int kFinalShift = 31;

} // namespace

EnvironmentContext::EnvironmentContext(TimerScheduler &timers, EnvironmentSink &sink,
                                       Allocator &allocator, std::uint64_t random_seed) noexcept
    : timers_{&timers}, sink_{&sink}, allocator_{&allocator}, random_state_{random_seed} {}

TimerId EnvironmentContext::set_timer(Timestamp monotonic_now, Duration delay,
                                      TimerTag tag) noexcept {
  VOLT_ASSERT(delay.ns() >= 0, "actor timer delay is negative");
  const expected<Timestamp> deadline = monotonic_now.checked_add(delay);
  VOLT_ASSERT(deadline.has_value(), "actor timer deadline left the timestamp range");
  return timers_->schedule(*deadline, tag);
}

void EnvironmentContext::cancel_timer(TimerId timer) noexcept { timers_->cancel(timer); }

void EnvironmentContext::publish(TopicId topic, PayloadView payload) noexcept {
  sink_->publish(topic, payload);
}

RequestId EnvironmentContext::call(ServiceId service, MethodId method, PayloadView payload,
                                   Duration timeout) noexcept {
  VOLT_ASSERT(next_request_id_ != std::numeric_limits<std::uint64_t>::max(),
              "request identifier space exhausted");
  const RequestId request{next_request_id_};
  ++next_request_id_;
  sink_->call(request, service, method, payload, timeout);
  return request;
}

void EnvironmentContext::respond(RequestId request, PayloadView payload) noexcept {
  sink_->respond(request, payload);
}

std::uint64_t EnvironmentContext::random() noexcept {
  random_state_ += kGoldenGamma;
  std::uint64_t result = random_state_;
  result = (result ^ (result >> kFirstShift)) * kFirstMultiplier;
  result = (result ^ (result >> kSecondShift)) * kSecondMultiplier;
  return result ^ (result >> kFinalShift);
}

void EnvironmentContext::log(Level level, std::string_view message, LogArgs args) noexcept {
  sink_->log(level, message, args);
}

void EnvironmentContext::trace(TraceEventId event, std::uint64_t arg) noexcept {
  sink_->trace(event, arg);
}

Allocator &EnvironmentContext::allocator() noexcept { return *allocator_; }

} // namespace volt::actor
