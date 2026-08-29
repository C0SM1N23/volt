#include "volt/actor/dispatcher.hpp"
#include "volt/actor/environment_context.hpp"
#include "volt/actor/environment_sink.hpp"
#include "volt/actor/real_environment.hpp"
#include "volt/actor/sim_environment.hpp"
#include "volt/actor/timer_queue.hpp"

#include "volt/core/hash.hpp"
#include "volt/memory/alignment.hpp"
#include "volt/memory/arena.hpp"
#include "volt/memory/byte_count.hpp"
#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/sim/sim_config.hpp"
#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::actor {
namespace {

// Small powers of two exercise wrapping and capacity boundaries while keeping
// every test case directly inspectable. Changing them changes only test depth.
constexpr std::size_t kMailboxCapacity = 2;
constexpr std::size_t kTimerCapacity = 4;
constexpr std::size_t kDispatchBudget = 8;
// The EchoActor schema introduced by P11. Incrementing it deliberately makes
// old records fail validation instead of being interpreted as the new layout.
constexpr StateVersion kEchoStateVersion{1};
// The event-order actor has an independent schema so its lifecycle contract is
// exercised with real state persistence rather than empty test doubles.
constexpr StateVersion kOrderStateVersion{1};
// Fixed seed required for byte-identical RealEnvironment/SimEnvironment input.
// Changing it produces another deterministic stream and no semantic change.
constexpr std::uint64_t kEnvironmentSeed = 0x91B4'1EC7'273A'55D2ULL;
// EchoActor identifiers are fixed test protocol values. Changing one changes
// the expected deterministic input stream but not runtime configuration.
constexpr TimerTag kEchoTimerTag{7};
constexpr MethodId kEchoMethod{5};
constexpr TraceEventId kEchoTraceEvent{9};
// This buffer bounds the test actor's complete state record. Reducing it below
// the schema size makes serialization fail loudly; increasing it changes no behavior.
constexpr std::size_t kStateStorageBytes = 64;
// Actor-owned scratch capacity used only to prove Environment returns the
// injected arena. It is not a runtime default.
constexpr std::size_t kArenaStorageBytes = 128;

constexpr expected<memory::Alignment> kActorAlignment =
    memory::Alignment::create(memory::ByteCount::from_bytes(alignof(std::max_align_t)));
static_assert(kActorAlignment.has_value());

class RecordingSink final : public EnvironmentSink {
public:
  void publish(TopicId topic, PayloadView payload) noexcept override {
    ++publications;
    last_topic = topic;
    last_payload_size = payload.size();
  }

  void call(RequestId request, ServiceId service, MethodId method, PayloadView payload,
            Duration timeout) noexcept override {
    ++calls;
    last_request = request;
    last_service = service;
    last_method = method;
    last_payload_size = payload.size();
    last_timeout = timeout;
  }

  void respond(RequestId request, PayloadView payload) noexcept override {
    ++responses;
    last_request = request;
    last_payload_size = payload.size();
  }

  void log(Level level, std::string_view message, LogArgs args) noexcept override {
    ++logs;
    last_level = level;
    last_message_size = message.size();
    last_argument_count = args.size();
  }

  void trace(TraceEventId event, std::uint64_t arg) noexcept override {
    ++traces;
    last_trace = event;
    last_trace_argument = arg;
  }

  std::size_t publications = 0;
  std::size_t calls = 0;
  std::size_t responses = 0;
  std::size_t logs = 0;
  std::size_t traces = 0;
  std::size_t last_payload_size = 0;
  std::size_t last_message_size = 0;
  std::size_t last_argument_count = 0;
  TopicId last_topic{};
  RequestId last_request{};
  ServiceId last_service{};
  MethodId last_method{};
  Duration last_timeout{};
  Level last_level = Level::kTrace;
  TraceEventId last_trace{};
  std::uint64_t last_trace_argument = 0;
};

class EchoActor final : public IActor {
public:
  void on_start(Environment &environment) override {
    state_.accumulator = environment.random();
    state_.timer = environment.set_timer(Duration{}, kEchoTimerTag);
    ++state_.starts;
  }

  void on_message(const Message &message, Environment &environment) override {
    VOLT_ASSERT(message.payload.size() == 1, "EchoActor accepts one-byte test inputs");
    state_.accumulator ^= std::to_integer<std::uint8_t>(message.payload.front());
    state_.accumulator ^= environment.random();
    environment.publish(message.topic, message.payload);
    state_.request =
        environment.call(ServiceId{3}, kEchoMethod, message.payload, Duration::from_ms(2));
    environment.respond(state_.request, message.payload);
    const std::array<std::uint64_t, 1> arguments{state_.accumulator};
    environment.log(Level::kInfo, "echo", arguments);
    environment.trace(kEchoTraceEvent, state_.accumulator);
    state_.arena_capacity = environment.allocator().capacity_bytes().bytes();
    ++state_.messages;
  }

  void on_timer(TimerId timer, TimerTag tag, Environment &environment) override {
    state_.accumulator ^= timer.value();
    state_.accumulator ^= tag.value();
    environment.cancel_timer(timer);
    ++state_.timers;
  }

  void on_stop(Environment &environment) noexcept override {
    state_.accumulator ^= environment.random();
    ++state_.stops;
  }

  void serialize(StateWriter &writer) const override {
    VOLT_ASSERT(writer.write_u64(state_.accumulator).has_value(),
                "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u64(state_.timer.value()).has_value(),
                "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u64(state_.request.value()).has_value(),
                "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u64(state_.arena_capacity).has_value(),
                "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u32(state_.starts).has_value(), "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u32(state_.messages).has_value(), "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u32(state_.timers).has_value(), "EchoActor state buffer is short");
    VOLT_ASSERT(writer.write_u32(state_.stops).has_value(), "EchoActor state buffer is short");
  }

  void deserialize(StateReader &reader) override {
    const expected<std::uint64_t> accumulator = reader.read_u64();
    const expected<std::uint64_t> timer = reader.read_u64();
    const expected<std::uint64_t> request = reader.read_u64();
    const expected<std::uint64_t> arena_capacity = reader.read_u64();
    const expected<std::uint32_t> starts = reader.read_u32();
    const expected<std::uint32_t> messages = reader.read_u32();
    const expected<std::uint32_t> timers = reader.read_u32();
    const expected<std::uint32_t> stops = reader.read_u32();
    VOLT_ASSERT(accumulator && timer && request && arena_capacity && starts && messages && timers &&
                    stops,
                "EchoActor state record is truncated");
    state_ = State{.accumulator = *accumulator,
                   .timer = TimerId{*timer},
                   .request = RequestId{*request},
                   .arena_capacity = *arena_capacity,
                   .starts = *starts,
                   .messages = *messages,
                   .timers = *timers,
                   .stops = *stops};
  }

  [[nodiscard]] Hash state_hash() const noexcept override {
    std::array<std::byte, kStateStorageBytes> storage{};
    expected<StateWriter> writer = StateWriter::create(storage, kEchoStateVersion);
    VOLT_ASSERT(writer.has_value(), "EchoActor state header does not fit");
    serialize(*writer);
    return core::xxhash64(writer->finish());
  }

private:
  struct State {
    std::uint64_t accumulator = 0;
    TimerId timer{};
    RequestId request{};
    std::uint64_t arena_capacity = 0;
    std::uint32_t starts = 0;
    std::uint32_t messages = 0;
    std::uint32_t timers = 0;
    std::uint32_t stops = 0;
  };

  State state_{};
};

class OrderActor final : public IActor {
public:
  enum class Event : std::uint8_t { kMessage, kTimer };

  void on_start([[maybe_unused]] Environment &environment) override {}

  void on_message([[maybe_unused]] const Message &message,
                  [[maybe_unused]] Environment &environment) override {
    record(Event::kMessage);
  }

  void on_timer([[maybe_unused]] TimerId timer, [[maybe_unused]] TimerTag tag,
                [[maybe_unused]] Environment &environment) override {
    record(Event::kTimer);
  }

  void on_stop([[maybe_unused]] Environment &environment) noexcept override {}

  void serialize(StateWriter &writer) const override {
    VOLT_ASSERT(writer.write_u64(static_cast<std::uint64_t>(event_count)).has_value(),
                "OrderActor state buffer is short");
    const std::span<const Event> event_span{events};
    VOLT_ASSERT(writer.write_bytes(std::as_bytes(event_span)).has_value(),
                "OrderActor state buffer is short");
  }

  void deserialize(StateReader &reader) override {
    const expected<std::uint64_t> count = reader.read_u64();
    const expected<std::span<const std::byte>> serialized_events =
        reader.read_bytes(events.size() * sizeof(Event));
    VOLT_ASSERT(count && serialized_events && *count <= events.size(),
                "OrderActor state record is invalid");
    std::span<std::byte> event_bytes = std::as_writable_bytes(std::span<Event>{events});
    std::ranges::copy(*serialized_events, event_bytes.begin());
    event_count = static_cast<std::size_t>(*count);
  }

  [[nodiscard]] Hash state_hash() const noexcept override {
    std::array<std::byte, kStateStorageBytes> storage{};
    expected<StateWriter> writer = StateWriter::create(storage, kOrderStateVersion);
    VOLT_ASSERT(writer.has_value(), "OrderActor state header does not fit");
    serialize(*writer);
    return core::xxhash64(writer->finish());
  }

  std::array<Event, kDispatchBudget> events{};
  std::size_t event_count = 0;

private:
  void record(Event event) noexcept {
    VOLT_ASSERT(event_count < events.size(), "OrderActor event capacity exhausted");
    events[event_count] = event;
    ++event_count;
  }
};

[[nodiscard]] Message message_at(std::int64_t timestamp_ns, TopicId topic,
                                 PayloadView payload) noexcept {
  return Message{.timestamp = Timestamp::from_ns_since_epoch(timestamp_ns),
                 .topic = topic,
                 .payload = payload};
}

// @verifies REQ-PLT-030
TEST(ActorEnvironmentTest, ProducesIdenticalStateAcrossRealAndSimBackendsAtEveryStep) {
  pal::posix::PosixPlatform real_platform;
  const pal::sim::SimConfig sim_config{.seed = kEnvironmentSeed};
  pal::sim::SimPlatform sim_platform{sim_config};
  TimerQueue<kTimerCapacity> real_timers;
  TimerQueue<kTimerCapacity> sim_timers;
  RecordingSink real_sink;
  RecordingSink sim_sink;
  std::array<std::byte, kArenaStorageBytes> real_storage{};
  std::array<std::byte, kArenaStorageBytes> sim_storage{};
  memory::Arena real_arena{real_storage, *kActorAlignment};
  memory::Arena sim_arena{sim_storage, *kActorAlignment};
  EnvironmentContext real_context{real_timers, real_sink, real_arena, kEnvironmentSeed};
  EnvironmentContext sim_context{sim_timers, sim_sink, sim_arena, kEnvironmentSeed};
  RealEnvironment real_environment{real_platform, real_context};
  SimEnvironment sim_environment{sim_platform, sim_context};
  EchoActor real_actor;
  EchoActor sim_actor;
  Dispatcher<kMailboxCapacity, kDispatchBudget> real_dispatcher{
      real_actor, real_environment, real_timers, MailboxFullPolicy::kFault};
  Dispatcher<kMailboxCapacity, kDispatchBudget> sim_dispatcher{
      sim_actor, sim_environment, sim_timers, MailboxFullPolicy::kFault};

  EXPECT_GT(real_environment.now().ns_since_epoch(), 0);
  EXPECT_EQ(sim_environment.now().ns_since_epoch(), sim_config.realtime_offset.ns());
  EXPECT_EQ(&real_environment.allocator(), &real_arena);
  EXPECT_EQ(&sim_environment.allocator(), &sim_arena);

  real_dispatcher.start();
  sim_dispatcher.start();
  EXPECT_EQ(real_actor.state_hash(), sim_actor.state_hash());

  const DispatchReport real_timer_report = real_dispatcher.run_ready(real_environment.mono());
  const DispatchReport sim_timer_report = sim_dispatcher.run_ready(sim_environment.mono());
  EXPECT_EQ(real_timer_report.timers, 1U);
  EXPECT_EQ(sim_timer_report.timers, 1U);
  EXPECT_EQ(real_actor.state_hash(), sim_actor.state_hash());

  const std::array<std::byte, 1> payload{std::byte{0x5A}};
  ASSERT_TRUE(real_dispatcher
                  .enqueue(Message{.timestamp = real_environment.mono(),
                                   .topic = TopicId{11},
                                   .payload = payload})
                  .has_value());
  ASSERT_TRUE(sim_dispatcher
                  .enqueue(Message{.timestamp = sim_environment.mono(),
                                   .topic = TopicId{11},
                                   .payload = payload})
                  .has_value());
  const DispatchReport real_message_report = real_dispatcher.run_ready(real_environment.mono());
  const DispatchReport sim_message_report = sim_dispatcher.run_ready(sim_environment.mono());
  EXPECT_EQ(real_message_report.messages, 1U);
  EXPECT_EQ(sim_message_report.messages, 1U);
  EXPECT_EQ(real_actor.state_hash(), sim_actor.state_hash());

  real_dispatcher.stop();
  sim_dispatcher.stop();
  EXPECT_EQ(real_actor.state_hash(), sim_actor.state_hash());
  EXPECT_EQ(real_sink.publications, sim_sink.publications);
  EXPECT_EQ(real_sink.calls, sim_sink.calls);
  EXPECT_EQ(real_sink.responses, sim_sink.responses);
  EXPECT_EQ(real_sink.logs, sim_sink.logs);
  EXPECT_EQ(real_sink.traces, sim_sink.traces);
  EXPECT_EQ(real_sink.last_topic, TopicId{11});
  EXPECT_EQ(real_sink.last_request, RequestId{1});
  EXPECT_EQ(real_sink.last_service, ServiceId{3});
  EXPECT_EQ(real_sink.last_method, kEchoMethod);
  EXPECT_EQ(real_sink.last_payload_size, payload.size());
  EXPECT_EQ(real_sink.last_timeout, Duration::from_ms(2));
  EXPECT_EQ(real_sink.last_level, Level::kInfo);
  EXPECT_EQ(real_sink.last_message_size, std::string_view{"echo"}.size());
  EXPECT_EQ(real_sink.last_argument_count, 1U);
  EXPECT_EQ(real_sink.last_trace, kEchoTraceEvent);
  EXPECT_EQ(real_sink.last_trace_argument, sim_sink.last_trace_argument);
}

TEST(MailboxTest, DropsOldestAndRetainsNewestWhenFull) {
  const std::array<std::byte, 1> first_payload{std::byte{1}};
  const std::array<std::byte, 1> second_payload{std::byte{2}};
  const std::array<std::byte, 1> third_payload{std::byte{3}};
  Mailbox<kMailboxCapacity> mailbox{MailboxFullPolicy::kDropOldest};
  ASSERT_TRUE(mailbox.push(message_at(1, TopicId{1}, first_payload)).has_value());
  ASSERT_TRUE(mailbox.push(message_at(2, TopicId{2}, second_payload)).has_value());
  ASSERT_TRUE(mailbox.push(message_at(3, TopicId{3}, third_payload)).has_value());

  ASSERT_EQ(mailbox.size(), kMailboxCapacity);
  EXPECT_EQ(mailbox.stats().accepted, 3U);
  EXPECT_EQ(mailbox.stats().dropped_oldest, 1U);
  ASSERT_TRUE(mailbox.peek().has_value());
  EXPECT_EQ(mailbox.pop()->topic, TopicId{2});
  EXPECT_EQ(mailbox.pop()->topic, TopicId{3});
  EXPECT_TRUE(mailbox.empty());
}

TEST(MailboxTest, DropsNewMessageAndCountsLossWhenFull) {
  const std::array<std::byte, 1> payload{std::byte{1}};
  Mailbox<1> mailbox{MailboxFullPolicy::kDropNew};
  ASSERT_TRUE(mailbox.push(message_at(1, TopicId{1}, payload)).has_value());
  const expected<void> rejected = mailbox.push(message_at(2, TopicId{2}, payload));

  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), core::ErrorCode::kTransientMessageLost);
  EXPECT_EQ(mailbox.stats().accepted, 1U);
  EXPECT_EQ(mailbox.stats().dropped_new, 1U);
  EXPECT_EQ(mailbox.pop()->topic, TopicId{1});
}

TEST(MailboxTest, ReportsAndCountsFaultWhenFull) {
  const std::array<std::byte, 1> payload{std::byte{1}};
  Mailbox<1> mailbox{MailboxFullPolicy::kFault};
  EXPECT_EQ(mailbox.full_policy(), MailboxFullPolicy::kFault);
  ASSERT_TRUE(mailbox.push(message_at(1, TopicId{1}, payload)).has_value());
  const expected<void> rejected = mailbox.push(message_at(2, TopicId{2}, payload));

  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), core::ErrorCode::kResourceExhausted);
  EXPECT_EQ(mailbox.stats().faults, 1U);
  EXPECT_EQ(mailbox.capacity(), 1U);
}

TEST(MailboxTest, ReportsUnavailableWhenEmpty) {
  Mailbox<1> mailbox{MailboxFullPolicy::kFault};
  EXPECT_EQ(mailbox.peek().error(), core::ErrorCode::kResourceUnavailable);
  EXPECT_EQ(mailbox.pop().error(), core::ErrorCode::kResourceUnavailable);
}

TEST(TimerQueueTest, OrdersByDeadlineThenIdentifierAndSupportsCancellation) {
  TimerQueue<kTimerCapacity> timers;
  const TimerId later = timers.schedule(Timestamp::from_ns_since_epoch(20), TimerTag{20});
  const TimerId first_tie = timers.schedule(Timestamp::from_ns_since_epoch(10), TimerTag{1});
  const TimerId second_tie = timers.schedule(Timestamp::from_ns_since_epoch(10), TimerTag{2});
  timers.cancel(later);

  ASSERT_TRUE(timers.peek().has_value());
  EXPECT_EQ(timers.pop()->id, first_tie);
  EXPECT_EQ(timers.pop()->id, second_tie);
  EXPECT_EQ(timers.size(), 0U);
  timers.cancel(later);
  EXPECT_EQ(timers.cancel_misses(), 1U);
}

TEST(TimerQueueTest, CountsCapacityAndEmptyPopFailures) {
  TimerQueue<1> timers;
  EXPECT_EQ(timers.capacity(), 1U);
  EXPECT_NE(timers.schedule(Timestamp{}, TimerTag{1}), TimerId{});
  EXPECT_EQ(timers.schedule(Timestamp{}, TimerTag{2}), TimerId{});
  EXPECT_EQ(timers.schedule_failures(), 1U);
  ASSERT_TRUE(timers.pop().has_value());
  EXPECT_EQ(timers.pop().error(), core::ErrorCode::kResourceUnavailable);
  EXPECT_EQ(timers.pop_failures(), 1U);
}

TEST(DispatcherTest, DeliversMessageBeforeTimerAtSameTimestamp) {
  pal::sim::SimPlatform platform{pal::sim::SimConfig{}};
  TimerQueue<kTimerCapacity> timers;
  RecordingSink sink;
  std::array<std::byte, kArenaStorageBytes> arena_storage{};
  memory::Arena arena{arena_storage, *kActorAlignment};
  EnvironmentContext context{timers, sink, arena, kEnvironmentSeed};
  SimEnvironment environment{platform, context};
  OrderActor actor;
  Dispatcher<kMailboxCapacity, kDispatchBudget> dispatcher{actor, environment, timers,
                                                           MailboxFullPolicy::kFault};
  const std::array<std::byte, 1> payload{std::byte{1}};
  const Timestamp deadline = Timestamp::from_ns_since_epoch(10);
  ASSERT_NE(timers.schedule(deadline, TimerTag{1}), TimerId{});
  ASSERT_TRUE(
      dispatcher.enqueue(Message{.timestamp = deadline, .topic = TopicId{1}, .payload = payload})
          .has_value());

  dispatcher.start();
  const DispatchReport report = dispatcher.run_ready(deadline);
  dispatcher.stop();

  ASSERT_EQ(actor.event_count, 2U);
  EXPECT_EQ(actor.events[0], OrderActor::Event::kMessage);
  EXPECT_EQ(actor.events[1], OrderActor::Event::kTimer);
  EXPECT_EQ(report.messages, 1U);
  EXPECT_EQ(report.timers, 1U);
  EXPECT_FALSE(report.budget_exhausted);
  EXPECT_EQ(dispatcher.pending_messages(), 0U);
  EXPECT_EQ(dispatcher.mailbox_stats().accepted, 1U);
}

TEST(DispatcherTest, StopsAtCompileTimeBudgetAndLeavesDueWorkPending) {
  pal::sim::SimPlatform platform{pal::sim::SimConfig{}};
  TimerQueue<kTimerCapacity> timers;
  RecordingSink sink;
  std::array<std::byte, kArenaStorageBytes> arena_storage{};
  memory::Arena arena{arena_storage, *kActorAlignment};
  EnvironmentContext context{timers, sink, arena, kEnvironmentSeed};
  SimEnvironment environment{platform, context};
  OrderActor actor;
  Dispatcher<kMailboxCapacity, 1> dispatcher{actor, environment, timers, MailboxFullPolicy::kFault};
  const std::array<std::byte, 1> payload{std::byte{1}};
  ASSERT_TRUE(dispatcher.enqueue(message_at(0, TopicId{1}, payload)).has_value());
  ASSERT_TRUE(dispatcher.enqueue(message_at(0, TopicId{2}, payload)).has_value());

  dispatcher.start();
  const DispatchReport report = dispatcher.run_ready(Timestamp{});
  dispatcher.stop();

  EXPECT_EQ(report.messages, 1U);
  EXPECT_TRUE(report.budget_exhausted);
  EXPECT_EQ(dispatcher.pending_messages(), 1U);
}

TEST(DispatcherTest, RejectsRestartAfterStop) {
  pal::sim::SimPlatform platform{pal::sim::SimConfig{}};
  TimerQueue<kTimerCapacity> timers;
  RecordingSink sink;
  std::array<std::byte, kArenaStorageBytes> arena_storage{};
  memory::Arena arena{arena_storage, *kActorAlignment};
  EnvironmentContext context{timers, sink, arena, kEnvironmentSeed};
  SimEnvironment environment{platform, context};
  OrderActor actor;
  Dispatcher<kMailboxCapacity, kDispatchBudget> dispatcher{actor, environment, timers,
                                                           MailboxFullPolicy::kFault};

  dispatcher.start();
  dispatcher.stop();

  EXPECT_DEATH(dispatcher.start(), "");
}

} // namespace
} // namespace volt::actor
