#pragma once

#include "volt/pal/platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
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

  /// Builds a name that two test processes on one machine cannot share, for
  /// resources living in a kernel-global namespace.
  [[nodiscard]] std::string unique_name(std::string_view stem) {
    return std::string{stem} + "-" + std::to_string(platform().current_process_id());
  }

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

TYPED_TEST_P(PalConformance, ThreadCpuTimeAdvancesWithWork) {
  IClock &clock = this->platform().clock();
  const std::int64_t before = clock.thread_cpu().ns_since_epoch();
  // Enough iterations that even a coarse CPU clock ticks; the loop's result
  // is observed so the work cannot be optimised away.
  volatile std::uint64_t sink = 0;
  for (std::uint64_t iteration = 0; iteration < 2'000'000; ++iteration) {
    sink = sink + iteration;
  }
  const std::int64_t after = clock.thread_cpu().ns_since_epoch();
  EXPECT_GE(after, before);
  if (after == before) {
    // A virtual backend only moves time through waiting; burning host CPU
    // must not advance it, so equality is the correct answer there.
    const core::expected<void> waited = clock.sleep_for(kShortInterval);
    ASSERT_TRUE(waited.has_value());
    EXPECT_GE(clock.thread_cpu().ns_since_epoch(), before);
  }
}

TYPED_TEST_P(PalConformance, SleepingCostsNoMeaningfulThreadCpu) {
  IClock &clock = this->platform().clock();
  const std::int64_t wall_before = clock.monotonic().ns_since_epoch();
  const std::int64_t cpu_before = clock.thread_cpu().ns_since_epoch();
  ASSERT_TRUE(clock.sleep_for(core::Duration::from_ms(50)).has_value());
  const std::int64_t wall_delta = clock.monotonic().ns_since_epoch() - wall_before;
  const std::int64_t cpu_delta = clock.thread_cpu().ns_since_epoch() - cpu_before;

  ASSERT_GE(wall_delta, core::Duration::from_ms(50).ns());
  // Waiting is free, working is not: the nap may cost bookkeeping, never
  // anything on the order of the nap itself.
  EXPECT_LT(cpu_delta, wall_delta / 2);
}

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

TYPED_TEST_P(PalConformance, DisarmingReachesABlockedWait) {
  core::expected<std::unique_ptr<ITimer>> timer = this->platform().create_timer();
  ASSERT_TRUE(timer.has_value());
  // Far enough away that the wait below is a real block, not a race with
  // the expiration.
  ASSERT_TRUE((*timer)->arm_once(core::Duration::from_s(30)).has_value());

  std::atomic<bool> wait_failed{false};
  core::expected<std::unique_ptr<IThread>> waiter = this->platform().create_thread(
      ThreadConfig{.name = "volt-wait",
                   .policy = SchedulingPolicy::kOther,
                   .priority = core::Priority{0},
                   .cpu_mask = 0,
                   .stack_bytes = 0},
      [&timer, &wait_failed] {
        wait_failed.store(!(*timer)->wait().has_value(), std::memory_order_release);
      });
  ASSERT_TRUE(waiter.has_value());

  // Give a real backend a moment to block; a virtual one runs the body at
  // join, after the disarm, and must reach the same verdict.
  ASSERT_TRUE(this->platform().clock().sleep_for(kShortInterval).has_value());
  ASSERT_TRUE((*timer)->disarm().has_value());
  ASSERT_TRUE((*waiter)->join().has_value());

  EXPECT_TRUE(wait_failed.load(std::memory_order_acquire))
      << "a disarmed timer left its waiter sleeping or handed it an expiration";
}

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

TYPED_TEST_P(PalConformance, SharedMemoryIsAlignedForOveralignedObjects) {
  core::expected<std::unique_ptr<ISharedMemory>> region =
      this->platform().create_shared_memory("volt-conformance-aligned", 4096);
  ASSERT_TRUE(region.has_value());

  // A cache line is the alignment shared layouts actually ask for, since
  // padding two cursors apart is how they avoid false sharing. Anything
  // less from a backend would make such a layout undefined behaviour on it
  // while working on the other.
  constexpr std::uintptr_t kCacheLine = 64;
  const auto address = reinterpret_cast<std::uintptr_t>((*region)->bytes().data());
  EXPECT_EQ(address % kCacheLine, 0U) << "a shared region began mid cache line";
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

TYPED_TEST_P(PalConformance, ADestroyedPeerReadsAsEndOfStream) {
  core::expected<std::unique_ptr<IStreamListener>> listener = this->platform().listen_stream(
      Endpoint{.address = kLoopbackAddress, .port = 0}, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  const core::expected<Endpoint> where = (*listener)->local_endpoint();
  ASSERT_TRUE(where.has_value());

  core::expected<std::unique_ptr<IStreamSocket>> server = [&] {
    core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_stream(*where);
    EXPECT_TRUE(client.has_value());
    core::expected<std::unique_ptr<IStreamSocket>> accepted = (*listener)->accept();
    // The client dies here without a word, as processes do.
    return accepted;
  }();
  ASSERT_TRUE(server.has_value());

  ASSERT_TRUE((*server)->set_receive_timeout(kReceiveTimeout).has_value());
  std::array<std::byte, 8> buffer{};
  const core::expected<std::size_t> received = (*server)->receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, 0U) << "a closed peer is end of stream, not an error";
}

TYPED_TEST_P(PalConformance, LocalStreamCarriesBytesBothWays) {
  const std::string path = TypeParam::writable_path("bytes_both_ways.sock");
  core::expected<std::unique_ptr<IStreamListener>> listener =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());

  core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_local(path);
  ASSERT_TRUE(client.has_value());
  core::expected<std::unique_ptr<IStreamSocket>> server = (*listener)->accept();
  ASSERT_TRUE(server.has_value());

  const std::array<std::byte, 3> ping{std::byte{'p'}, std::byte{'n'}, std::byte{'g'}};
  ASSERT_TRUE((*client)->send(ping).has_value());
  std::array<std::byte, 8> heard{};
  ASSERT_TRUE((*server)->set_receive_timeout(kReceiveTimeout).has_value());
  const core::expected<std::size_t> at_server = (*server)->receive(heard);
  ASSERT_TRUE(at_server.has_value());
  EXPECT_EQ(*at_server, ping.size());
  EXPECT_TRUE(std::equal(ping.begin(), ping.end(), heard.begin()));

  const std::array<std::byte, 2> pong{std::byte{'o'}, std::byte{'k'}};
  ASSERT_TRUE((*server)->send(pong).has_value());
  ASSERT_TRUE((*client)->set_receive_timeout(kReceiveTimeout).has_value());
  const core::expected<std::size_t> at_client = (*client)->receive(heard);
  ASSERT_TRUE(at_client.has_value());
  EXPECT_EQ(*at_client, pong.size());
  EXPECT_TRUE(std::equal(pong.begin(), pong.end(), heard.begin()));
}

TYPED_TEST_P(PalConformance, LocalConnectionIsEstablishedBeforeItIsAccepted) {
  const std::string path = TypeParam::writable_path("established_first.sock");
  core::expected<std::unique_ptr<IStreamListener>> listener =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());

  // One thread connects and only then accepts; anything else deadlocks here.
  core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_local(path);
  ASSERT_TRUE(client.has_value());
  EXPECT_TRUE((*listener)->accept().has_value());
}

TYPED_TEST_P(PalConformance, ConnectingToAMissingLocalPathReportsAnError) {
  const std::string path = TypeParam::writable_path("nobody_here.sock");
  const core::expected<std::unique_ptr<IStreamSocket>> client =
      this->platform().connect_local(path);
  ASSERT_FALSE(client.has_value());
  EXPECT_EQ(client.error(), core::ErrorCode::kTransientPeerUnreachable);
}

TYPED_TEST_P(PalConformance, ListeningTwiceOnALivePathReportsBusy) {
  const std::string path = TypeParam::writable_path("taken.sock");
  core::expected<std::unique_ptr<IStreamListener>> first =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(first.has_value());

  const core::expected<std::unique_ptr<IStreamListener>> second =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), core::ErrorCode::kResourceBusy);
}

TYPED_TEST_P(PalConformance, AClosedListenersPathCanBeListenedOnAgain) {
  const std::string path = TypeParam::writable_path("reused.sock");
  core::expected<std::unique_ptr<IStreamListener>> first =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(first.has_value());
  first->reset();

  EXPECT_TRUE(this->platform().listen_local(path, kDefaultListenBacklog).has_value());
}

TYPED_TEST_P(PalConformance, LocalPeerCredentialsIdentifyThisProcess) {
  const std::string path = TypeParam::writable_path("credentials.sock");
  core::expected<std::unique_ptr<IStreamListener>> listener =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_local(path);
  ASSERT_TRUE(client.has_value());
  core::expected<std::unique_ptr<IStreamSocket>> server = (*listener)->accept();
  ASSERT_TRUE(server.has_value());

  // Both ends live in this process, so the kernel-attested identity has to
  // be this process on both, and the two ends have to agree on the user.
  const core::expected<PeerCredentials> seen_by_server = (*server)->peer_credentials();
  const core::expected<PeerCredentials> seen_by_client = (*client)->peer_credentials();
  ASSERT_TRUE(seen_by_server.has_value());
  ASSERT_TRUE(seen_by_client.has_value());
  EXPECT_EQ(seen_by_server->process_id, this->platform().current_process_id());
  EXPECT_EQ(seen_by_client->process_id, this->platform().current_process_id());
  EXPECT_EQ(seen_by_server->user_id, seen_by_client->user_id);
  EXPECT_EQ(seen_by_server->group_id, seen_by_client->group_id);
}

TYPED_TEST_P(PalConformance, TcpStreamHasNoPeerCredentials) {
  core::expected<std::unique_ptr<IStreamListener>> listener = this->platform().listen_stream(
      Endpoint{.address = kLoopbackAddress, .port = 0}, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  const core::expected<Endpoint> where = (*listener)->local_endpoint();
  ASSERT_TRUE(where.has_value());
  core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_stream(*where);
  ASSERT_TRUE(client.has_value());

  const core::expected<PeerCredentials> identity = (*client)->peer_credentials();
  ASSERT_FALSE(identity.has_value());
  EXPECT_EQ(identity.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST_P(PalConformance, LocalStreamHasNoTcpEndpoints) {
  const std::string path = TypeParam::writable_path("no_endpoints.sock");
  core::expected<std::unique_ptr<IStreamListener>> listener =
      this->platform().listen_local(path, kDefaultListenBacklog);
  ASSERT_TRUE(listener.has_value());
  core::expected<std::unique_ptr<IStreamSocket>> client = this->platform().connect_local(path);
  ASSERT_TRUE(client.has_value());

  // Identity of a local stream is the path and the credentials; an address
  // would be an invention.
  EXPECT_FALSE((*listener)->local_endpoint().has_value());
  EXPECT_FALSE((*client)->peer_endpoint().has_value());
}

TYPED_TEST_P(PalConformance, TheCurrentProcessIsAlive) {
  EXPECT_TRUE(this->platform().process_alive(this->platform().current_process_id()));
}

TYPED_TEST_P(PalConformance, AReapedChildIsNoLongerAlive) {
  const ProcessConfig config{.executable = TypeParam::succeeding_program(), .arguments = {}};
  core::expected<std::unique_ptr<IProcess>> child = this->platform().spawn_process(config);
  ASSERT_TRUE(child.has_value());
  const std::int32_t identifier = (*child)->id();

  // Existence spans spawn to reap: even after the child exits it remains
  // observable until someone waits for it, which is the kernel's contract.
  EXPECT_TRUE(this->platform().process_alive(identifier));
  ASSERT_TRUE((*child)->wait().has_value());
  EXPECT_FALSE(this->platform().process_alive(identifier));
}

TYPED_TEST_P(PalConformance, MessageQueueRoundTripsAMessage) {
  const std::string name = this->unique_name("mq-round-trip");
  core::expected<std::unique_ptr<IMessageQueue>> created = this->platform().create_message_queue(
      MessageQueueConfig{.name = name, .depth = 4, .message_bytes = 64});
  ASSERT_TRUE(created.has_value());
  core::expected<std::unique_ptr<IMessageQueue>> opened = this->platform().open_message_queue(name);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ((*opened)->depth(), 4U);
  EXPECT_EQ((*opened)->message_bytes(), 64U);

  const std::array<std::byte, 4> sent{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  ASSERT_TRUE((*created)->send(sent).has_value());
  std::array<std::byte, 64> heard{};
  ASSERT_TRUE((*opened)->set_receive_timeout(kReceiveTimeout).has_value());
  const core::expected<std::size_t> received = (*opened)->receive(heard);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, sent.size());
  EXPECT_TRUE(std::equal(sent.begin(), sent.end(), heard.begin()));
}

TYPED_TEST_P(PalConformance, MessageQueueReceiveTimesOutWhenEmpty) {
  const std::string name = this->unique_name("mq-timeout");
  core::expected<std::unique_ptr<IMessageQueue>> queue = this->platform().create_message_queue(
      MessageQueueConfig{.name = name, .depth = 2, .message_bytes = 16});
  ASSERT_TRUE(queue.has_value());
  ASSERT_TRUE((*queue)->set_receive_timeout(kReceiveTimeout).has_value());

  std::array<std::byte, 16> buffer{};
  const core::expected<std::size_t> received = (*queue)->receive(buffer);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(received.error(), core::ErrorCode::kTransientTimeout);
}

TYPED_TEST_P(PalConformance, MessageQueueSendReportsExhaustionWhenFull) {
  const std::string name = this->unique_name("mq-full");
  core::expected<std::unique_ptr<IMessageQueue>> queue = this->platform().create_message_queue(
      MessageQueueConfig{.name = name, .depth = 2, .message_bytes = 16});
  ASSERT_TRUE(queue.has_value());

  const std::array<std::byte, 1> message{std::byte{7}};
  ASSERT_TRUE((*queue)->send(message).has_value());
  ASSERT_TRUE((*queue)->send(message).has_value());
  // The third send meets a full queue; waiting for the consumer is exactly
  // what the contract rules out.
  const core::expected<void> third = (*queue)->send(message);
  ASSERT_FALSE(third.has_value());
  EXPECT_EQ(third.error(), core::ErrorCode::kResourceExhausted);
}

TYPED_TEST_P(PalConformance, MessageQueueRejectsAnOversizedMessage) {
  const std::string name = this->unique_name("mq-oversized");
  core::expected<std::unique_ptr<IMessageQueue>> queue = this->platform().create_message_queue(
      MessageQueueConfig{.name = name, .depth = 2, .message_bytes = 8});
  ASSERT_TRUE(queue.has_value());

  const std::array<std::byte, 9> message{};
  const core::expected<void> sent = (*queue)->send(message);
  ASSERT_FALSE(sent.has_value());
  EXPECT_EQ(sent.error(), core::ErrorCode::kInternalBufferTooSmall);
}

TYPED_TEST_P(PalConformance, MessageQueueRejectsAZeroGeometry) {
  const core::expected<std::unique_ptr<IMessageQueue>> no_depth =
      this->platform().create_message_queue(
          MessageQueueConfig{.name = "mq-zero-depth", .depth = 0, .message_bytes = 16});
  ASSERT_FALSE(no_depth.has_value());
  EXPECT_EQ(no_depth.error(), core::ErrorCode::kConfigValueOutOfRange);

  const core::expected<std::unique_ptr<IMessageQueue>> no_bytes =
      this->platform().create_message_queue(
          MessageQueueConfig{.name = "mq-zero-bytes", .depth = 2, .message_bytes = 0});
  ASSERT_FALSE(no_bytes.has_value());
  EXPECT_EQ(no_bytes.error(), core::ErrorCode::kConfigValueOutOfRange);
}

TYPED_TEST_P(PalConformance, OpeningAMissingMessageQueueReportsAnError) {
  const core::expected<std::unique_ptr<IMessageQueue>> opened =
      this->platform().open_message_queue(this->unique_name("mq-never-created"));
  ASSERT_FALSE(opened.has_value());
  EXPECT_EQ(opened.error(), core::ErrorCode::kResourceUnavailable);
}

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

TYPED_TEST_P(PalConformance, DeadlinePolicyRejectsAnUnorderedReservation) {
  // The sporadic model is 0 < runtime <= deadline <= period. Each of these
  // breaks one link of that chain, and none of them may reach the kernel.
  const std::array<DeadlineParameters, 4> impossible{
      DeadlineParameters{.runtime = core::Duration::from_us(0),
                         .deadline = core::Duration::from_ms(1),
                         .period = core::Duration::from_ms(1)},
      DeadlineParameters{.runtime = core::Duration::from_ms(2),
                         .deadline = core::Duration::from_ms(1),
                         .period = core::Duration::from_ms(1)},
      DeadlineParameters{.runtime = core::Duration::from_us(100),
                         .deadline = core::Duration::from_ms(2),
                         .period = core::Duration::from_ms(1)},
      DeadlineParameters{.runtime = core::Duration::from_us(100),
                         .deadline = core::Duration::from_us(200),
                         .period = core::Duration::from_ms(0)}};

  for (std::size_t index = 0; index < impossible.size(); ++index) {
    const core::expected<void> refused =
        this->platform().set_current_thread_deadline(impossible[index]);
    ASSERT_FALSE(refused.has_value()) << "reservation " << index;
    EXPECT_EQ(refused.error(), core::ErrorCode::kConfigValueOutOfRange) << "reservation " << index;
  }
}

TYPED_TEST_P(PalConformance, DeadlinePolicyEitherAdmitsOrExplainsItself) {
  // A modest reservation: a tenth of every ten milliseconds. Whether the
  // kernel grants it depends on privilege and on bandwidth already
  // committed, so the contract is not "it works" but "it answers, and the
  // answer is one a caller can act on".
  const DeadlineParameters modest{.runtime = core::Duration::from_ms(1),
                                  .deadline = core::Duration::from_ms(10),
                                  .period = core::Duration::from_ms(10)};
  const core::expected<void> granted = this->platform().set_current_thread_deadline(modest);
  if (granted.has_value()) {
    SUCCEED() << "the reservation was admitted";
    return;
  }
  EXPECT_TRUE(granted.error() == core::ErrorCode::kResourceUnavailable ||
              granted.error() == core::ErrorCode::kResourceBusy)
      << "a refused reservation must say whether it was permission or bandwidth";
}

TYPED_TEST_P(PalConformance, DeadlinePolicyIsNotReachableAsAPriority) {
  // Two ways in would mean two places to get the reservation wrong.
  const core::expected<void> refused = this->platform().set_current_thread_scheduling(
      SchedulingPolicy::kDeadline, core::Priority{0});
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), core::ErrorCode::kConfigValueOutOfRange);
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
    ThreadCpuTimeAdvancesWithWork, SleepingCostsNoMeaningfulThreadCpu, ThreadRunsItsEntryPoint,
    ThreadIsNotJoinableAfterJoining, JoiningATwiceJoinedThreadReportsAnError,
    ThreadKeepsTheNameItWasGiven, ThreadNameIsTruncatedRatherThanRejected,
    DefaultPolicyRejectsANonZeroPriority, RealTimePriorityFailsGracefullyWithoutPermission,
    RealTimePolicyRejectsAPriorityOutOfRange, SeveralThreadsAllRunToCompletion,
    ThreadAcceptsAnExplicitStackSize, WaitingOnAnUnarmedTimerReportsAnError,
    OneShotTimerRejectsAZeroDelay, PeriodicTimerRejectsAZeroPeriod, OneShotTimerFires,
    OneShotTimerDoesNotAdvanceTheClockBackwards, PeriodicTimerFiresRepeatedly,
    DisarmingATimerMakesWaitingReportAnError, DisarmingReachesABlockedWait,
    SharedMemoryRejectsAZeroSize, SharedMemoryHasTheRequestedSize, SharedMemoryStartsZeroed,
    SharedMemoryReportsItsName, SharedMemoryIsAlignedForOveralignedObjects,
    SharedMemoryIsVisibleThroughASecondMapping, OpeningAnUnknownSharedMemoryNameReportsAnError,
    BindingAssignsAnEphemeralPort, LocalEndpointIsUnknownBeforeBinding, BindingTwiceReportsAnError,
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
    ADestroyedPeerReadsAsEndOfStream, LocalStreamCarriesBytesBothWays,
    LocalConnectionIsEstablishedBeforeItIsAccepted, ConnectingToAMissingLocalPathReportsAnError,
    ListeningTwiceOnALivePathReportsBusy, AClosedListenersPathCanBeListenedOnAgain,
    LocalPeerCredentialsIdentifyThisProcess, TcpStreamHasNoPeerCredentials,
    LocalStreamHasNoTcpEndpoints, TheCurrentProcessIsAlive, AReapedChildIsNoLongerAlive,
    MessageQueueRoundTripsAMessage, MessageQueueReceiveTimesOutWhenEmpty,
    MessageQueueSendReportsExhaustionWhenFull, MessageQueueRejectsAnOversizedMessage,
    MessageQueueRejectsAZeroGeometry, OpeningAMissingMessageQueueReportsAnError,
    OpeningAMissingWatchdogReportsAnError, WatchdogRejectsAZeroTimeout, WatchdogAcceptsBeingPetted,
    LockingMemoryEitherSucceedsOrReportsWhyNot, DeadlinePolicyRejectsAnUnorderedReservation,
    DeadlinePolicyEitherAdmitsOrExplainsItself, DeadlinePolicyIsNotReachableAsAPriority,
    PromotingTheCurrentThreadRejectsABadPriority,
    PromotingTheCurrentThreadRejectsAPriorityOnTheDefaultPolicy);

} // namespace volt::pal::conformance
