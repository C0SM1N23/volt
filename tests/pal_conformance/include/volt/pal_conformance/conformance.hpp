#pragma once

#include "volt/pal/platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The contract every PAL backend has to satisfy, written once.
///
/// The suite is type-parameterized so a backend instantiates it from its own
/// translation unit without this file changing. That is what keeps the POSIX,
/// simulation and QNX backends honest against a single definition of correct
/// behaviour instead of three suites that drift apart.
///
/// Every test here states a contract, never a mechanism: it asks what the
/// caller is promised, not how the backend arranges it. A test that needed to
/// know whether a real thread or a cooperative one ran would not belong here.
///
/// A backend supplies these static members:
///   `create_platform()`      a fresh IPlatform
///   `writable_path(name)`    a path the test may create and overwrite
///   `succeeding_program()`   a program that exits with status zero
///   `failing_program()`      a program that exits with a non-zero status
///   `missing_program()`      a path where no program exists
///   `watchdog_path()`        the watchdog device
///   `provides_watchdog()`    whether the watchdog tests can run
namespace volt::pal::conformance {

/// 127.0.0.1 in host order. Every socket test stays on the loopback so it
/// neither depends on nor disturbs the machine's real network.
inline constexpr std::uint32_t kLoopbackAddress = 0x7F00'0001U;

/// Long enough that a correct implementation never hits it, short enough that
/// a broken one fails the suite quickly rather than hanging the run.
inline constexpr core::Duration kReceiveTimeout = core::Duration::from_ms(200);

/// Timers are asked for short intervals so the whole suite stays under a
/// second; correctness here is about ordering and counting, not accuracy.
inline constexpr core::Duration kShortInterval = core::Duration::from_ms(2);

template <typename Backend> class PalConformance : public ::testing::Test {
protected:
  void SetUp() override { platform_ = Backend::create_platform(); }

  [[nodiscard]] IPlatform &platform() noexcept { return *platform_; }

  /// Binds a datagram socket to an ephemeral loopback port.
  [[nodiscard]] std::unique_ptr<ISocket> bound_socket() {
    core::expected<std::unique_ptr<ISocket>> socket = platform().create_datagram_socket();
    EXPECT_TRUE(socket.has_value());
    const core::expected<void> bound =
        (*socket)->bind(Endpoint{.address = kLoopbackAddress, .port = 0});
    EXPECT_TRUE(bound.has_value());
    return std::move(*socket);
  }

  /// Starts a listener on an ephemeral loopback port.
  [[nodiscard]] std::unique_ptr<IStreamListener> listener() {
    core::expected<std::unique_ptr<IStreamListener>> listening = platform().listen_stream(
        Endpoint{.address = kLoopbackAddress, .port = 0}, kDefaultListenBacklog);
    EXPECT_TRUE(listening.has_value());
    return std::move(*listening);
  }

  /// Connects to a listener that this fixture started.
  [[nodiscard]] std::unique_ptr<IStreamSocket> connect_to(IStreamListener &target) {
    core::expected<std::unique_ptr<IStreamSocket>> client =
        platform().connect_stream(*target.local_endpoint());
    EXPECT_TRUE(client.has_value());
    return std::move(*client);
  }

private:
  std::unique_ptr<IPlatform> platform_;
};

TYPED_TEST_SUITE_P(PalConformance);

// ---------------------------------------------------------------- clock ----

TYPED_TEST_P(PalConformance, MonotonicClockNeverGoesBackwards) {
  const core::Timestamp first = this->platform().clock().monotonic();
  const core::Timestamp second = this->platform().clock().monotonic();
  EXPECT_GE(second, first);
}

TYPED_TEST_P(PalConformance, MonotonicClockAdvancesAcrossASleep) {
  IClock &clock = this->platform().clock();
  const core::Timestamp before = clock.monotonic();
  ASSERT_TRUE(clock.sleep_for(kShortInterval).has_value());
  const core::Timestamp after = clock.monotonic();

  const core::expected<core::Duration> elapsed = after.checked_since(before);
  ASSERT_TRUE(elapsed.has_value());
  EXPECT_GE(elapsed->ns(), kShortInterval.ns());
}

TYPED_TEST_P(PalConformance, RealtimeClockIsPastTheEpoch) {
  EXPECT_GT(this->platform().clock().realtime().ns_since_epoch(), 0);
}

TYPED_TEST_P(PalConformance, SleepRejectsANegativeDelay) {
  const core::expected<void> result =
      this->platform().clock().sleep_for(core::Duration::from_ms(-1));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, SleepAcceptsAZeroDelay) {
  EXPECT_TRUE(this->platform().clock().sleep_for(core::Duration{}).has_value());
}

// --------------------------------------------------------------- thread ----

TYPED_TEST_P(PalConformance, ThreadRunsItsEntryPoint) {
  int ran = 0;
  core::expected<std::unique_ptr<IThread>> thread =
      this->platform().create_thread(ThreadConfig{.name = "volt-entry"}, [&ran] { ran = 1; });

  ASSERT_TRUE(thread.has_value());
  ASSERT_TRUE((*thread)->join().has_value());
  EXPECT_EQ(ran, 1);
}

TYPED_TEST_P(PalConformance, ThreadIsNotJoinableAfterJoining) {
  core::expected<std::unique_ptr<IThread>> thread =
      this->platform().create_thread(ThreadConfig{.name = "volt-join"}, [] {});

  ASSERT_TRUE(thread.has_value());
  EXPECT_TRUE((*thread)->joinable());
  ASSERT_TRUE((*thread)->join().has_value());
  EXPECT_FALSE((*thread)->joinable());
}

TYPED_TEST_P(PalConformance, JoiningATwiceJoinedThreadReportsAnError) {
  core::expected<std::unique_ptr<IThread>> thread =
      this->platform().create_thread(ThreadConfig{.name = "volt-twice"}, [] {});

  ASSERT_TRUE(thread.has_value());
  ASSERT_TRUE((*thread)->join().has_value());
  const core::expected<void> again = (*thread)->join();

  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kInternalOutOfRange);
}

TYPED_TEST_P(PalConformance, ThreadKeepsTheNameItWasGiven) {
  core::expected<std::unique_ptr<IThread>> thread =
      this->platform().create_thread(ThreadConfig{.name = "volt-named"}, [] {});

  ASSERT_TRUE(thread.has_value());
  EXPECT_EQ((*thread)->name(), "volt-named");
  EXPECT_TRUE((*thread)->join().has_value());
}

TYPED_TEST_P(PalConformance, ThreadNameIsTruncatedRatherThanRejected) {
  // A label is never a reason to refuse to start a service.
  constexpr std::string_view kOverlongName = "volt-an-extremely-long-thread-name";
  core::expected<std::unique_ptr<IThread>> thread =
      this->platform().create_thread(ThreadConfig{.name = kOverlongName}, [] {});

  ASSERT_TRUE(thread.has_value());
  EXPECT_LT((*thread)->name().size(), kOverlongName.size());
  EXPECT_TRUE(kOverlongName.starts_with((*thread)->name()));
  EXPECT_TRUE((*thread)->join().has_value());
}

TYPED_TEST_P(PalConformance, DefaultPolicyRejectsANonZeroPriority) {
  // Asking for a priority under the time-sharing policy means the caller
  // believed it was requesting real-time behaviour and would not get it.
  core::expected<std::unique_ptr<IThread>> thread = this->platform().create_thread(
      ThreadConfig{
          .name = "volt-prio", .policy = SchedulingPolicy::kOther, .priority = core::Priority{10}},
      [] {});

  ASSERT_FALSE(thread.has_value());
  EXPECT_EQ(thread.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, RealTimePriorityFailsGracefullyWithoutPermission) {
  // Running without the privilege to set a real-time policy must produce an
  // ErrorCode, never a crash: an unprivileged developer machine has to be able
  // to run the same binary as the target.
  constexpr core::Priority kControlThreadPriority{80};
  core::expected<std::unique_ptr<IThread>> thread = this->platform().create_thread(
      ThreadConfig{
          .name = "volt-rt", .policy = SchedulingPolicy::kFifo, .priority = kControlThreadPriority},
      [] {});

  if (thread.has_value()) {
    EXPECT_TRUE((*thread)->join().has_value());
    SUCCEED();
    return;
  }
  EXPECT_EQ(core::category(thread.error()), core::ErrorCategory::kResource);
}

TYPED_TEST_P(PalConformance, RealTimePolicyRejectsAPriorityOutOfRange) {
  constexpr core::Priority kImpossiblePriority{200};
  core::expected<std::unique_ptr<IThread>> thread = this->platform().create_thread(
      ThreadConfig{
          .name = "volt-range", .policy = SchedulingPolicy::kFifo, .priority = kImpossiblePriority},
      [] {});

  ASSERT_FALSE(thread.has_value());
  EXPECT_EQ(thread.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, SeveralThreadsAllRunToCompletion) {
  constexpr int kThreadCount = 4;
  std::array<int, kThreadCount> ran{};
  std::vector<std::unique_ptr<IThread>> threads;

  for (int index = 0; index < kThreadCount; ++index) {
    core::expected<std::unique_ptr<IThread>> thread =
        this->platform().create_thread(ThreadConfig{.name = "volt-many"},
                                       [&ran, index] { ran[static_cast<std::size_t>(index)] = 1; });
    ASSERT_TRUE(thread.has_value());
    threads.push_back(std::move(*thread));
  }
  for (const std::unique_ptr<IThread> &thread : threads) {
    EXPECT_TRUE(thread->join().has_value());
  }
  EXPECT_EQ(ran, (std::array<int, kThreadCount>{1, 1, 1, 1}));
}

TYPED_TEST_P(PalConformance, ThreadAcceptsAnExplicitStackSize) {
  // Large enough that every platform accepts it, so the test measures that the
  // request is honoured rather than how small a stack the system allows.
  constexpr std::size_t kStackBytes = std::size_t{512} * 1024;
  int ran = 0;
  core::expected<std::unique_ptr<IThread>> thread = this->platform().create_thread(
      ThreadConfig{.name = "volt-stack", .stack_bytes = kStackBytes}, [&ran] { ran = 1; });

  ASSERT_TRUE(thread.has_value());
  ASSERT_TRUE((*thread)->join().has_value());
  EXPECT_EQ(ran, 1);
}

// ---------------------------------------------------------------- timer ----

TYPED_TEST_P(PalConformance, WaitingOnAnUnarmedTimerReportsAnError) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());

  const core::expected<std::uint64_t> result = (*timer)->wait();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, OneShotTimerRejectsAZeroDelay) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());

  const core::expected<void> result = (*timer)->arm_once(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, PeriodicTimerRejectsAZeroPeriod) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());

  const core::expected<void> result = (*timer)->arm_periodic(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, OneShotTimerFires) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());
  ASSERT_TRUE((*timer)->arm_once(kShortInterval).has_value());

  const core::expected<std::uint64_t> expirations = (*timer)->wait();
  ASSERT_TRUE(expirations.has_value());
  EXPECT_GE(*expirations, 1U);
}

TYPED_TEST_P(PalConformance, OneShotTimerDoesNotAdvanceTheClockBackwards) {
  IClock &clock = this->platform().clock();
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());

  const core::Timestamp before = clock.monotonic();
  ASSERT_TRUE((*timer)->arm_once(kShortInterval).has_value());
  ASSERT_TRUE((*timer)->wait().has_value());

  const core::expected<core::Duration> elapsed = clock.monotonic().checked_since(before);
  ASSERT_TRUE(elapsed.has_value());
  EXPECT_GE(elapsed->ns(), kShortInterval.ns());
}

TYPED_TEST_P(PalConformance, PeriodicTimerFiresRepeatedly) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());
  ASSERT_TRUE((*timer)->arm_periodic(kShortInterval).has_value());

  EXPECT_TRUE((*timer)->wait().has_value());
  EXPECT_TRUE((*timer)->wait().has_value());
  EXPECT_TRUE((*timer)->wait().has_value());
}

TYPED_TEST_P(PalConformance, DisarmingATimerMakesWaitingReportAnError) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());
  ASSERT_TRUE((*timer)->arm_periodic(kShortInterval).has_value());
  ASSERT_TRUE((*timer)->disarm().has_value());

  const core::expected<std::uint64_t> result = (*timer)->wait();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kResourceUnavailable);
}

// -------------------------------------------------------- shared memory ----

TYPED_TEST_P(PalConformance, SharedMemoryRejectsAZeroSize) {
  const core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().create_shared_memory("volt-zero", 0);

  ASSERT_FALSE(region.has_value());
  EXPECT_EQ(region.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, SharedMemoryHasTheRequestedSize) {
  constexpr std::size_t kRegionBytes = 4096;
  core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().create_shared_memory("volt-size", kRegionBytes);

  ASSERT_TRUE(region.has_value());
  EXPECT_EQ((*region)->bytes().size(), kRegionBytes);
}

TYPED_TEST_P(PalConformance, SharedMemoryStartsZeroed) {
  constexpr std::size_t kRegionBytes = 256;
  core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().create_shared_memory("volt-zeroed", kRegionBytes);

  ASSERT_TRUE(region.has_value());
  const std::span<const std::byte> bytes = (*region)->bytes();
  EXPECT_EQ(std::count(bytes.begin(), bytes.end(), std::byte{0}), kRegionBytes);
}

TYPED_TEST_P(PalConformance, SharedMemoryReportsItsName) {
  core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().create_shared_memory("volt-named-region", 64);

  ASSERT_TRUE(region.has_value());
  EXPECT_FALSE((*region)->name().empty());
}

TYPED_TEST_P(PalConformance, SharedMemoryIsVisibleThroughASecondMapping) {
  constexpr std::size_t kRegionBytes = 128;
  constexpr std::byte kMarker{0xAB};
  core::expected<std::unique_ptr<ISharedMemory>> created =
      this->platform().create_shared_memory("volt-shared", kRegionBytes);
  ASSERT_TRUE(created.has_value());

  core::expected<std::unique_ptr<ISharedMemory>> opened =
      this->platform().open_shared_memory("volt-shared");
  ASSERT_TRUE(opened.has_value());

  (*created)->bytes()[0] = kMarker;
  EXPECT_EQ((*opened)->bytes()[0], kMarker);
}

TYPED_TEST_P(PalConformance, OpeningAnUnknownSharedMemoryNameReportsAnError) {
  const core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().open_shared_memory("volt-never-created");

  ASSERT_FALSE(region.has_value());
  EXPECT_EQ(region.error(), core::ErrorCode::kResourceUnavailable);
}

// --------------------------------------------------------------- socket ----

TYPED_TEST_P(PalConformance, BindingAssignsAnEphemeralPort) {
  const std::unique_ptr<ISocket> socket = this->bound_socket();

  const core::expected<Endpoint> local = socket->local_endpoint();
  ASSERT_TRUE(local.has_value());
  EXPECT_NE(local->port, 0);
}

TYPED_TEST_P(PalConformance, LocalEndpointIsUnknownBeforeBinding) {
  core::expected<std::unique_ptr<ISocket>> socket = this->platform().create_datagram_socket();
  ASSERT_TRUE(socket.has_value());

  const core::expected<Endpoint> local = (*socket)->local_endpoint();
  ASSERT_FALSE(local.has_value());
  EXPECT_EQ(local.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, BindingTwiceReportsAnError) {
  const std::unique_ptr<ISocket> socket = this->bound_socket();

  const core::expected<void> again = socket->bind(Endpoint{.address = kLoopbackAddress, .port = 0});
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kResourceBusy);
}

TYPED_TEST_P(PalConformance, DatagramArrivesAtItsDestination) {
  const std::unique_ptr<ISocket> sender = this->bound_socket();
  const std::unique_ptr<ISocket> receiver = this->bound_socket();
  ASSERT_TRUE(receiver->set_receive_timeout(kReceiveTimeout).has_value());

  const core::expected<Endpoint> destination = receiver->local_endpoint();
  ASSERT_TRUE(destination.has_value());

  constexpr std::array<std::byte, 4> kPayload{std::byte{1}, std::byte{2}, std::byte{3},
                                              std::byte{4}};
  const core::expected<std::size_t> sent = sender->send_to(kPayload, *destination);
  ASSERT_TRUE(sent.has_value());
  EXPECT_EQ(*sent, kPayload.size());

  std::array<std::byte, 16> buffer{};
  const core::expected<Datagram> received = receiver->receive_from(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->bytes, kPayload.size());
  EXPECT_EQ(buffer[0], kPayload[0]);
  EXPECT_EQ(buffer[3], kPayload[3]);
}

TYPED_TEST_P(PalConformance, ReceivedDatagramCarriesTheSenderEndpoint) {
  const std::unique_ptr<ISocket> sender = this->bound_socket();
  const std::unique_ptr<ISocket> receiver = this->bound_socket();
  ASSERT_TRUE(receiver->set_receive_timeout(kReceiveTimeout).has_value());

  const core::expected<Endpoint> sender_endpoint = sender->local_endpoint();
  const core::expected<Endpoint> destination = receiver->local_endpoint();
  ASSERT_TRUE(sender_endpoint.has_value());
  ASSERT_TRUE(destination.has_value());

  constexpr std::array<std::byte, 1> kPayload{std::byte{7}};
  ASSERT_TRUE(sender->send_to(kPayload, *destination).has_value());

  std::array<std::byte, 8> buffer{};
  const core::expected<Datagram> received = receiver->receive_from(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->from.port, sender_endpoint->port);
}

TYPED_TEST_P(PalConformance, ReceiveTimesOutWhenNothingArrives) {
  const std::unique_ptr<ISocket> receiver = this->bound_socket();
  ASSERT_TRUE(receiver->set_receive_timeout(kShortInterval).has_value());

  std::array<std::byte, 8> buffer{};
  const core::expected<Datagram> received = receiver->receive_from(buffer);

  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(core::category(received.error()), core::ErrorCategory::kTransient);
}

TYPED_TEST_P(PalConformance, ReceiveTimeoutRejectsAZeroDuration) {
  const std::unique_ptr<ISocket> socket = this->bound_socket();

  const core::expected<void> result = socket->set_receive_timeout(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, DatagramLongerThanTheBufferIsTruncated) {
  const std::unique_ptr<ISocket> sender = this->bound_socket();
  const std::unique_ptr<ISocket> receiver = this->bound_socket();
  ASSERT_TRUE(receiver->set_receive_timeout(kReceiveTimeout).has_value());

  const core::expected<Endpoint> destination = receiver->local_endpoint();
  ASSERT_TRUE(destination.has_value());

  constexpr std::array<std::byte, 8> kPayload{};
  ASSERT_TRUE(sender->send_to(kPayload, *destination).has_value());

  std::array<std::byte, 2> buffer{};
  const core::expected<Datagram> received = receiver->receive_from(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_LE(received->bytes, buffer.size());
}

// ----------------------------------------------------------- stream ----

TYPED_TEST_P(PalConformance, ConnectingWhereNobodyListensReportsAnError) {
  const core::expected<std::unique_ptr<IStreamSocket>> client =
      this->platform().connect_stream(Endpoint{.address = kLoopbackAddress, .port = 1});

  ASSERT_FALSE(client.has_value());
  EXPECT_EQ(core::category(client.error()), core::ErrorCategory::kTransient);
}

TYPED_TEST_P(PalConformance, ListenerReportsItsEphemeralPort) {
  const std::unique_ptr<IStreamListener> listener = this->listener();

  const core::expected<Endpoint> local = listener->local_endpoint();
  ASSERT_TRUE(local.has_value());
  EXPECT_NE(local->port, 0);
}

TYPED_TEST_P(PalConformance, AcceptTimesOutWhenNobodyConnects) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  ASSERT_TRUE(listener->set_accept_timeout(kShortInterval).has_value());

  const core::expected<std::unique_ptr<IStreamSocket>> accepted = listener->accept();
  ASSERT_FALSE(accepted.has_value());
  EXPECT_EQ(core::category(accepted.error()), core::ErrorCategory::kTransient);
}

TYPED_TEST_P(PalConformance, AcceptTimeoutRejectsAZeroDuration) {
  const std::unique_ptr<IStreamListener> listener = this->listener();

  const core::expected<void> result = listener->set_accept_timeout(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, ConnectionIsEstablishedBeforeItIsAccepted) {
  // The whole reason a single thread can connect and then accept: the peer
  // does not have to be waiting in accept() for the connection to complete.
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);

  const core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  EXPECT_TRUE(client->peer_endpoint().has_value());
}

TYPED_TEST_P(PalConformance, StreamCarriesBytesFromClientToServer) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);
  core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  ASSERT_TRUE((*server)->set_receive_timeout(kReceiveTimeout).has_value());

  constexpr std::array<std::byte, 3> kPayload{std::byte{7}, std::byte{8}, std::byte{9}};
  ASSERT_TRUE(client->send(kPayload).has_value());

  std::array<std::byte, 8> buffer{};
  const core::expected<std::size_t> received = (*server)->receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, kPayload.size());
  EXPECT_EQ(buffer[0], kPayload[0]);
  EXPECT_EQ(buffer[2], kPayload[2]);
}

TYPED_TEST_P(PalConformance, StreamCarriesBytesFromServerToClient) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);
  core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  ASSERT_TRUE(client->set_receive_timeout(kReceiveTimeout).has_value());

  constexpr std::array<std::byte, 2> kPayload{std::byte{1}, std::byte{2}};
  ASSERT_TRUE((*server)->send(kPayload).has_value());

  std::array<std::byte, 8> buffer{};
  const core::expected<std::size_t> received = client->receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, kPayload.size());
}

TYPED_TEST_P(PalConformance, HalfClosingIsReportedAsEndOfStream) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);
  core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  ASSERT_TRUE((*server)->set_receive_timeout(kReceiveTimeout).has_value());

  ASSERT_TRUE(client->shutdown_send().has_value());

  std::array<std::byte, 4> buffer{};
  const core::expected<std::size_t> received = (*server)->receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, 0U);
}

TYPED_TEST_P(PalConformance, HalfClosingLeavesTheOtherDirectionOpen) {
  // A caller that has finished asking must still be able to read the answer.
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);
  core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  ASSERT_TRUE(client->set_receive_timeout(kReceiveTimeout).has_value());

  ASSERT_TRUE(client->shutdown_send().has_value());

  constexpr std::array<std::byte, 1> kAnswer{std::byte{42}};
  ASSERT_TRUE((*server)->send(kAnswer).has_value());

  std::array<std::byte, 4> buffer{};
  const core::expected<std::size_t> received = client->receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, kAnswer.size());
  EXPECT_EQ(buffer[0], kAnswer[0]);
}

TYPED_TEST_P(PalConformance, StreamReceiveTimesOutWhenNothingArrives) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);
  core::expected<std::unique_ptr<IStreamSocket>> server = listener->accept();
  ASSERT_TRUE(server.has_value());
  ASSERT_TRUE((*server)->set_receive_timeout(kShortInterval).has_value());

  std::array<std::byte, 4> buffer{};
  const core::expected<std::size_t> received = (*server)->receive(buffer);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(core::category(received.error()), core::ErrorCategory::kTransient);
}

TYPED_TEST_P(PalConformance, StreamReceiveTimeoutRejectsAZeroDuration) {
  const std::unique_ptr<IStreamListener> listener = this->listener();
  const std::unique_ptr<IStreamSocket> client = this->connect_to(*listener);

  const core::expected<void> result = client->set_receive_timeout(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

// ----------------------------------------------------------------- file ----

TYPED_TEST_P(PalConformance, FileRoundTripsWhatWasWritten) {
  const std::string path = TypeParam::writable_path("round_trip.bin");
  constexpr std::array<std::byte, 3> kPayload{std::byte{9}, std::byte{8}, std::byte{7}};

  core::expected<std::unique_ptr<IFile>> writer =
      this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(writer.has_value());
  ASSERT_TRUE((*writer)->write(kPayload).has_value());
  ASSERT_TRUE((*writer)->flush().has_value());
  writer->reset();

  core::expected<std::unique_ptr<IFile>> reader = this->platform().open_file(path, FileMode::kRead);
  ASSERT_TRUE(reader.has_value());
  std::array<std::byte, 8> buffer{};
  const core::expected<std::size_t> read = (*reader)->read(buffer);

  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, kPayload.size());
  EXPECT_EQ(buffer[0], kPayload[0]);
  EXPECT_EQ(buffer[2], kPayload[2]);
}

TYPED_TEST_P(PalConformance, FileSizeReportsWhatWasWritten) {
  const std::string path = TypeParam::writable_path("size.bin");
  constexpr std::array<std::byte, 5> kPayload{};

  core::expected<std::unique_ptr<IFile>> writer =
      this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(writer.has_value());
  ASSERT_TRUE((*writer)->write(kPayload).has_value());
  ASSERT_TRUE((*writer)->flush().has_value());

  const core::expected<std::uint64_t> size = (*writer)->size();
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, kPayload.size());
}

TYPED_TEST_P(PalConformance, ReadingFromAWriteOnlyFileReportsAnError) {
  const std::string path = TypeParam::writable_path("write_only.bin");
  core::expected<std::unique_ptr<IFile>> file = this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(file.has_value());

  std::array<std::byte, 4> buffer{};
  const core::expected<std::size_t> read = (*file)->read(buffer);
  ASSERT_FALSE(read.has_value());
  EXPECT_EQ(read.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, WritingToAReadOnlyFileReportsAnError) {
  const std::string path = TypeParam::writable_path("read_only.bin");
  core::expected<std::unique_ptr<IFile>> created =
      this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(created.has_value());
  created->reset();

  core::expected<std::unique_ptr<IFile>> file = this->platform().open_file(path, FileMode::kRead);
  ASSERT_TRUE(file.has_value());

  constexpr std::array<std::byte, 1> kPayload{};
  const core::expected<std::size_t> written = (*file)->write(kPayload);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, OpeningForWritingTruncatesExistingContent) {
  const std::string path = TypeParam::writable_path("truncate.bin");
  constexpr std::array<std::byte, 6> kFirst{};
  constexpr std::array<std::byte, 2> kSecond{};

  core::expected<std::unique_ptr<IFile>> first = this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE((*first)->write(kFirst).has_value());
  first->reset();

  core::expected<std::unique_ptr<IFile>> second =
      this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE((*second)->write(kSecond).has_value());
  ASSERT_TRUE((*second)->flush().has_value());

  const core::expected<std::uint64_t> size = (*second)->size();
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, kSecond.size());
}

TYPED_TEST_P(PalConformance, OpeningForAppendingKeepsExistingContent) {
  const std::string path = TypeParam::writable_path("append.bin");
  constexpr std::array<std::byte, 3> kFirst{};
  constexpr std::array<std::byte, 2> kSecond{};

  core::expected<std::unique_ptr<IFile>> first = this->platform().open_file(path, FileMode::kWrite);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE((*first)->write(kFirst).has_value());
  first->reset();

  core::expected<std::unique_ptr<IFile>> second =
      this->platform().open_file(path, FileMode::kAppend);
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE((*second)->write(kSecond).has_value());
  ASSERT_TRUE((*second)->flush().has_value());

  const core::expected<std::uint64_t> size = (*second)->size();
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, kFirst.size() + kSecond.size());
}

TYPED_TEST_P(PalConformance, OpeningAMissingFileForReadingReportsAnError) {
  const core::expected<std::unique_ptr<IFile>> file = this->platform().open_file(
      TypeParam::writable_path("definitely_absent.bin"), FileMode::kRead);

  ASSERT_FALSE(file.has_value());
  EXPECT_EQ(file.error(), core::ErrorCode::kResourceUnavailable);
}

// -------------------------------------------------------------- process ----

TYPED_TEST_P(PalConformance, SpawningAMissingProgramReportsAnError) {
  const ProcessConfig config{.executable = TypeParam::missing_program(), .arguments = {}};
  const core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);

  ASSERT_FALSE(child.has_value());
  EXPECT_EQ(core::category(child.error()), core::ErrorCategory::kResource);
}

TYPED_TEST_P(PalConformance, ProcessReportsASuccessfulExit) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());

  const core::expected<ProcessExit> exit = (*child)->wait();
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->reason, ExitReason::kReturned);
  EXPECT_EQ(exit->code, 0);
}

TYPED_TEST_P(PalConformance, ProcessReportsAFailingExit) {
  const ProcessConfig config{.executable = TypeParam::failing_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());

  const core::expected<ProcessExit> exit = (*child)->wait();
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->reason, ExitReason::kReturned);
  EXPECT_NE(exit->code, 0);
}

TYPED_TEST_P(PalConformance, ProcessIsNoLongerRunningAfterBeingWaitedFor) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());
  EXPECT_TRUE((*child)->running());

  ASSERT_TRUE((*child)->wait().has_value());
  EXPECT_FALSE((*child)->running());
}

TYPED_TEST_P(PalConformance, WaitingTwiceForAProcessReportsAnError) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());
  ASSERT_TRUE((*child)->wait().has_value());

  const core::expected<ProcessExit> again = (*child)->wait();
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, ProcessHasAnIdentifier) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());

  EXPECT_GT((*child)->id(), 0);
  EXPECT_TRUE((*child)->wait().has_value());
}

TYPED_TEST_P(PalConformance, SignallingAnExitedProcessReportsAnError) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());
  ASSERT_TRUE((*child)->wait().has_value());

  const core::expected<void> stopped = (*child)->request_stop();
  ASSERT_FALSE(stopped.has_value());
  EXPECT_EQ(stopped.error(), core::ErrorCode::kResourceUnavailable);
}

// ------------------------------------------------------------- watchdog ----

TYPED_TEST_P(PalConformance, OpeningAMissingWatchdogReportsAnError) {
  const core::expected<std::unique_ptr<IWatchdogDevice>> device =
      this->platform().open_watchdog("/volt-no-such-watchdog");

  ASSERT_FALSE(device.has_value());
  EXPECT_EQ(core::category(device.error()), core::ErrorCategory::kResource);
}

TYPED_TEST_P(PalConformance, WatchdogRejectsAZeroTimeout) {
  if (!TypeParam::provides_watchdog()) {
    GTEST_SKIP() << "backend has no watchdog device available";
  }
  core::expected<std::unique_ptr<IWatchdogDevice>> device =
      this->platform().open_watchdog(TypeParam::watchdog_path());
  ASSERT_TRUE(device.has_value());

  const core::expected<void> result = (*device)->set_timeout(core::Duration{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, WatchdogAcceptsBeingPetted) {
  if (!TypeParam::provides_watchdog()) {
    GTEST_SKIP() << "backend has no watchdog device available";
  }
  core::expected<std::unique_ptr<IWatchdogDevice>> device =
      this->platform().open_watchdog(TypeParam::watchdog_path());
  ASSERT_TRUE(device.has_value());

  EXPECT_TRUE((*device)->pet().has_value());
  EXPECT_TRUE((*device)->disable().has_value());
}

// ------------------------------------------------------ real-time setup ----

TYPED_TEST_P(PalConformance, LockingMemoryEitherSucceedsOrReportsWhyNot) {
  // Locking memory needs a privilege an unprivileged runner does not have. The
  // contract is that it says so, so startup can refuse rather than run with
  // page faults on the control path.
  const core::expected<void> result = this->platform().lock_memory();
  if (result.has_value()) {
    SUCCEED();
    return;
  }
  EXPECT_EQ(core::category(result.error()), core::ErrorCategory::kResource);
}

TYPED_TEST_P(PalConformance, PromotingTheCurrentThreadRejectsABadPriority) {
  constexpr core::Priority kImpossiblePriority{200};
  const core::expected<void> result =
      this->platform().set_current_thread_scheduling(SchedulingPolicy::kFifo, kImpossiblePriority);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, PromotingTheCurrentThreadRejectsAPriorityOnTheDefaultPolicy) {
  const core::expected<void> result =
      this->platform().set_current_thread_scheduling(SchedulingPolicy::kOther, core::Priority{5});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), core::ErrorCode::kConfigValueOutOfRange);
}

REGISTER_TYPED_TEST_SUITE_P(
    PalConformance, MonotonicClockNeverGoesBackwards, MonotonicClockAdvancesAcrossASleep,
    RealtimeClockIsPastTheEpoch, SleepRejectsANegativeDelay, SleepAcceptsAZeroDelay,
    ThreadRunsItsEntryPoint, ThreadIsNotJoinableAfterJoining,
    JoiningATwiceJoinedThreadReportsAnError, ThreadKeepsTheNameItWasGiven,
    ThreadNameIsTruncatedRatherThanRejected, DefaultPolicyRejectsANonZeroPriority,
    RealTimePriorityFailsGracefullyWithoutPermission, RealTimePolicyRejectsAPriorityOutOfRange,
    SeveralThreadsAllRunToCompletion, ThreadAcceptsAnExplicitStackSize,
    WaitingOnAnUnarmedTimerReportsAnError, OneShotTimerRejectsAZeroDelay,
    PeriodicTimerRejectsAZeroPeriod, OneShotTimerFires, OneShotTimerDoesNotAdvanceTheClockBackwards,
    PeriodicTimerFiresRepeatedly, DisarmingATimerMakesWaitingReportAnError,
    SharedMemoryRejectsAZeroSize, SharedMemoryHasTheRequestedSize, SharedMemoryStartsZeroed,
    SharedMemoryReportsItsName, SharedMemoryIsVisibleThroughASecondMapping,
    OpeningAnUnknownSharedMemoryNameReportsAnError, BindingAssignsAnEphemeralPort,
    LocalEndpointIsUnknownBeforeBinding, BindingTwiceReportsAnError,
    DatagramArrivesAtItsDestination, ReceivedDatagramCarriesTheSenderEndpoint,
    ReceiveTimesOutWhenNothingArrives, ReceiveTimeoutRejectsAZeroDuration,
    DatagramLongerThanTheBufferIsTruncated, FileRoundTripsWhatWasWritten,
    FileSizeReportsWhatWasWritten, ReadingFromAWriteOnlyFileReportsAnError,
    WritingToAReadOnlyFileReportsAnError, OpeningForWritingTruncatesExistingContent,
    OpeningForAppendingKeepsExistingContent, OpeningAMissingFileForReadingReportsAnError,
    SpawningAMissingProgramReportsAnError, ProcessReportsASuccessfulExit,
    ProcessReportsAFailingExit, ProcessIsNoLongerRunningAfterBeingWaitedFor,
    WaitingTwiceForAProcessReportsAnError, ProcessHasAnIdentifier,
    SignallingAnExitedProcessReportsAnError, ConnectingWhereNobodyListensReportsAnError,
    ListenerReportsItsEphemeralPort, AcceptTimesOutWhenNobodyConnects,
    AcceptTimeoutRejectsAZeroDuration, ConnectionIsEstablishedBeforeItIsAccepted,
    StreamCarriesBytesFromClientToServer, StreamCarriesBytesFromServerToClient,
    HalfClosingIsReportedAsEndOfStream, HalfClosingLeavesTheOtherDirectionOpen,
    StreamReceiveTimesOutWhenNothingArrives, StreamReceiveTimeoutRejectsAZeroDuration,
    OpeningAMissingWatchdogReportsAnError, WatchdogRejectsAZeroTimeout, WatchdogAcceptsBeingPetted,
    LockingMemoryEitherSucceedsOrReportsWhyNot, PromotingTheCurrentThreadRejectsABadPriority,
    PromotingTheCurrentThreadRejectsAPriorityOnTheDefaultPolicy);

} // namespace volt::pal::conformance
