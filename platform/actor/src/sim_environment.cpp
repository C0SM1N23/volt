#include "volt/actor/sim_environment.hpp"

namespace volt::actor {

SimEnvironment::SimEnvironment(pal::sim::SimPlatform &platform,
                               EnvironmentContext &context) noexcept
    : platform_{&platform}, context_{&context} {}

Timestamp SimEnvironment::now() const noexcept { return platform_->clock().realtime(); }

Timestamp SimEnvironment::mono() const noexcept { return platform_->clock().monotonic(); }

TimerId SimEnvironment::set_timer(Duration delay, TimerTag tag) noexcept {
  return context_->set_timer(mono(), delay, tag);
}

void SimEnvironment::cancel_timer(TimerId timer) noexcept { context_->cancel_timer(timer); }

void SimEnvironment::publish(TopicId topic, PayloadView payload) noexcept {
  context_->publish(topic, payload);
}

RequestId SimEnvironment::call(ServiceId service, MethodId method, PayloadView payload,
                               Duration timeout) noexcept {
  return context_->call(service, method, payload, timeout);
}

void SimEnvironment::respond(RequestId request, PayloadView payload) noexcept {
  context_->respond(request, payload);
}

std::uint64_t SimEnvironment::random() noexcept { return context_->random(); }

void SimEnvironment::log(Level level, std::string_view message, LogArgs args) noexcept {
  context_->log(level, message, args);
}

void SimEnvironment::trace(TraceEventId event, std::uint64_t arg) noexcept {
  context_->trace(event, arg);
}

Allocator &SimEnvironment::allocator() noexcept { return context_->allocator(); }

} // namespace volt::actor
