#include "volt/actor/real_environment.hpp"

namespace volt::actor {

RealEnvironment::RealEnvironment(pal::posix::PosixPlatform &platform,
                                 EnvironmentContext &context) noexcept
    : platform_{&platform}, context_{&context} {}

Timestamp RealEnvironment::now() const noexcept { return platform_->clock().realtime(); }

Timestamp RealEnvironment::mono() const noexcept { return platform_->clock().monotonic(); }

TimerId RealEnvironment::set_timer(Duration delay, TimerTag tag) noexcept {
  return context_->set_timer(mono(), delay, tag);
}

void RealEnvironment::cancel_timer(TimerId timer) noexcept { context_->cancel_timer(timer); }

void RealEnvironment::publish(TopicId topic, PayloadView payload) noexcept {
  context_->publish(topic, payload);
}

RequestId RealEnvironment::call(ServiceId service, MethodId method, PayloadView payload,
                                Duration timeout) noexcept {
  return context_->call(service, method, payload, timeout);
}

void RealEnvironment::respond(RequestId request, PayloadView payload) noexcept {
  context_->respond(request, payload);
}

std::uint64_t RealEnvironment::random() noexcept { return context_->random(); }

void RealEnvironment::log(Level level, std::string_view message, LogArgs args) noexcept {
  context_->log(level, message, args);
}

void RealEnvironment::trace(TraceEventId event, std::uint64_t arg) noexcept {
  context_->trace(event, arg);
}

Allocator &RealEnvironment::allocator() noexcept { return context_->allocator(); }

} // namespace volt::actor
