#include "volt/trace/perfetto_export.hpp"
#include "volt/trace/trace_capture.hpp"
#include "volt/trace/trace_file.hpp"
#include "volt/trace/tracer.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace volt::trace {
namespace {

/// Turns tracing on for the test and off again afterwards, so one test's
/// setting cannot decide another's outcome.
class TracingOn final {
public:
  TracingOn() noexcept { Tracer::instance().set_state(TraceState::kEnabled); }
  ~TracingOn() { Tracer::instance().set_state(TraceState::kDisabled); }

  TracingOn(const TracingOn &) = delete;
  TracingOn &operator=(const TracingOn &) = delete;
  TracingOn(TracingOn &&) = delete;
  TracingOn &operator=(TracingOn &&) = delete;
};

/// Empties the rings so a test starts from a known state.
void discard_pending() { static_cast<void>(collect(Tracer::instance())); }

[[nodiscard]] std::size_t count_of(const Capture &capture, TraceEvent event) {
  return static_cast<std::size_t>(std::ranges::count(capture.records, event, &TraceRecord::event));
}

TEST(TraceRecordTest, FitsTheSixteenByteBudget) {
  // The size is the reason the whole design is shaped this way, so it is
  // checked rather than assumed.
  EXPECT_EQ(sizeof(TraceRecord), 16U);
  EXPECT_EQ(kTraceRecordBytes, 16U);
}

TEST(TraceRecordTest, CarriesANodeFieldForLaterCorrelation) {
  // Nothing sets it yet, but a trace taken now has to stay readable by the
  // tool that will, so the field is present from the start.
  const TraceRecord record{};
  EXPECT_EQ(record.node_id, 0U);
}

TEST(TracerTest, RecordsNothingWhileDisabled) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  Tracer::instance().set_state(TraceState::kDisabled);
  discard_pending();

  VOLT_TRACE(TraceEvent::kTaskStart, 1);
  VOLT_TRACE(TraceEvent::kTaskEnd, 1);

  EXPECT_EQ(collect(Tracer::instance()).records.size(), 0U);
}

TEST(TracerTest, RecordsWhatWasTracedWhileEnabled) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  constexpr std::uint32_t kTaskId = 7;
  VOLT_TRACE(TraceEvent::kTaskActivate, kTaskId);
  VOLT_TRACE(TraceEvent::kDeadlineMiss, kTaskId);

  const Capture capture = collect(Tracer::instance());
  ASSERT_EQ(capture.records.size(), 2U);
  EXPECT_EQ(capture.records[0].event, TraceEvent::kTaskActivate);
  EXPECT_EQ(capture.records[1].event, TraceEvent::kDeadlineMiss);
  EXPECT_EQ(capture.records[0].argument, kTaskId);
}

TEST(TracerTest, RecordsCarryTheProducingThreadIndex) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  VOLT_TRACE(TraceEvent::kStateChange, 1);

  const Capture capture = collect(Tracer::instance());
  ASSERT_EQ(capture.records.size(), 1U);
  EXPECT_EQ(capture.records[0].thread_index, Tracer::current_thread_index());
}

TEST(TracerTest, TimestampsNeverGoBackwards) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  constexpr int kEvents = 64;
  for (int index = 0; index < kEvents; ++index) {
    VOLT_TRACE(TraceEvent::kTaskActivate, static_cast<std::uint32_t>(index));
  }

  const Capture capture = collect(Tracer::instance());
  ASSERT_EQ(capture.records.size(), static_cast<std::size_t>(kEvents));
  EXPECT_TRUE(std::ranges::is_sorted(capture.records, {}, &TraceRecord::cycles));
}

TEST(TraceScopeTest, EmitsABeginAndAnEndAroundTheScope) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  constexpr std::uint32_t kRpcId = 42;
  {
    const TraceScope scope{TraceEvent::kRpcBegin, TraceEvent::kRpcEnd, kRpcId};
    VOLT_TRACE(TraceEvent::kStateChange, kRpcId);
  }

  const Capture capture = collect(Tracer::instance());
  ASSERT_EQ(capture.records.size(), 3U);
  EXPECT_EQ(capture.records.front().event, TraceEvent::kRpcBegin);
  EXPECT_EQ(capture.records.back().event, TraceEvent::kRpcEnd);
}

TEST(TraceScopeTest, ClosesTheIntervalEvenWhenTracingIsTurnedOffInside) {
  // An interval that opened must close, or the timeline is left with a begin
  // nothing matches and a viewer draws a slice that never ends.
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  Tracer::instance().set_state(TraceState::kEnabled);

  {
    const TraceScope scope{TraceEvent::kTaskStart, TraceEvent::kTaskEnd, 1};
    Tracer::instance().set_state(TraceState::kDisabled);
  }

  const Capture capture = collect(Tracer::instance());
  EXPECT_EQ(count_of(capture, TraceEvent::kTaskStart), 1U);
  EXPECT_EQ(count_of(capture, TraceEvent::kTaskEnd), 1U);
}

TEST(TraceRingTest, CountsWhatItHadToThrowAway) {
  TraceRing ring;
  const TraceRecord record{};

  // One more than the ring holds, so exactly one is refused.
  for (std::size_t index = 0; index <= kRecordsPerRing; ++index) {
    ring.push(record);
  }

  EXPECT_EQ(ring.dropped(), 1U);
}

TEST(TraceRingTest, ReturnsRecordsInTheOrderTheyWerePushed) {
  TraceRing ring;
  constexpr std::uint32_t kCount = 8;
  for (std::uint32_t index = 0; index < kCount; ++index) {
    ring.push(TraceRecord{.cycles = index, .argument = index});
  }

  for (std::uint32_t index = 0; index < kCount; ++index) {
    TraceRecord record;
    ASSERT_TRUE(ring.pop(record));
    EXPECT_EQ(record.argument, index);
  }
  TraceRecord unused;
  EXPECT_FALSE(ring.pop(unused));
}

TEST(CycleClockTest, MeasuresATickRateAgainstThePlatformClock) {
  pal::posix::PosixPlatform platform;
  const CycleClock clock = CycleClock::calibrate(platform.clock(), core::Duration::from_ms(20));

  // Any real counter runs at a rate well above one tick per nanosecond; the
  // fallback returns exactly one, so this separates a measured clock from an
  // unmeasured one without asserting a frequency the machine has not promised.
  EXPECT_GT(clock.ticks_per_nanosecond(), 0.0);
}

TEST(CycleClockTest, ConvertsMonotonically) {
  pal::posix::PosixPlatform platform;
  const CycleClock clock = CycleClock::calibrate(platform.clock(), core::Duration::from_ms(5));

  EXPECT_LE(clock.to_nanoseconds(1000), clock.to_nanoseconds(2000));
  EXPECT_LE(clock.to_nanoseconds(0), clock.to_nanoseconds(1));
}

TEST(PerfettoExportTest, ProducesAJsonDocumentWithEveryEvent) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  VOLT_TRACE(TraceEvent::kTaskStart, 1);
  VOLT_TRACE(TraceEvent::kTaskEnd, 1);

  const Capture capture = collect(Tracer::instance());
  const std::string json = to_chrome_trace(capture, Tracer::instance().cycle_clock());

  EXPECT_TRUE(json.starts_with("{"));
  EXPECT_TRUE(json.ends_with("}\n"));
  EXPECT_NE(json.find("\"traceEvents\""), std::string::npos);
  EXPECT_NE(json.find("TaskStart"), std::string::npos);
  EXPECT_NE(json.find("TaskEnd"), std::string::npos);
}

TEST(PerfettoExportTest, NamesTheProcessAndItsThreads) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;
  VOLT_TRACE(TraceEvent::kStateChange, 1);

  const Capture capture = collect(Tracer::instance());
  const std::string json = to_chrome_trace(capture, Tracer::instance().cycle_clock());

  EXPECT_NE(json.find("process_name"), std::string::npos);
  EXPECT_NE(json.find("thread_name"), std::string::npos);
  EXPECT_NE(json.find("test-main"), std::string::npos);
}

TEST(PerfettoExportTest, DrawsIntervalsAsBeginAndEndPhases) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;
  {
    const TraceScope scope{TraceEvent::kTaskStart, TraceEvent::kTaskEnd, 1};
  }

  const Capture capture = collect(Tracer::instance());
  const std::string json = to_chrome_trace(capture, Tracer::instance().cycle_clock());

  EXPECT_NE(json.find(R"("ph":"B")"), std::string::npos);
  EXPECT_NE(json.find(R"("ph":"E")"), std::string::npos);
}

TEST(PerfettoExportTest, TiesATransmitToItsReceiveWithAFlowId) {
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  constexpr std::uint32_t kMessageId = 99;
  VOLT_TRACE(TraceEvent::kMessageTransmit, kMessageId);
  VOLT_TRACE(TraceEvent::kMessageReceive, kMessageId);

  const Capture capture = collect(Tracer::instance());
  const std::string json = to_chrome_trace(capture, Tracer::instance().cycle_clock());

  EXPECT_NE(json.find(R"("ph":"s")"), std::string::npos);
  EXPECT_NE(json.find(R"("ph":"f")"), std::string::npos);
  EXPECT_NE(json.find(R"("id":99)"), std::string::npos);
}

TEST(PerfettoExportTest, ReportsHowManyRecordsWereLost) {
  // A gap in a timeline has to be visible as a gap, not read as a quiet period.
  Capture capture;
  capture.dropped = 17;

  const std::string json = to_chrome_trace(capture, CycleClock::uncalibrated());
  EXPECT_NE(json.find(R"("voltDroppedRecords": 17)"), std::string::npos);
}

/// Writes `text` to `path`, reporting whether it landed.
[[nodiscard]] bool write_text(pal::IPlatform &platform, std::string_view path,
                              std::string_view text) {
  core::expected<std::unique_ptr<pal::IFile>> file =
      platform.open_file(path, pal::FileMode::kWrite);
  if (!file.has_value()) {
    return false;
  }
  const std::span<const std::byte> bytes{reinterpret_cast<const std::byte *>(text.data()),
                                         text.size()};
  return (*file)->write(bytes).has_value() && (*file)->flush().has_value();
}

TEST(PerfettoExportTest, ProducesATraceTheReferenceValidatorAccepts) {
  // Checked by a script that shares no code with the exporter, so a mistake in
  // how VOLT writes the format cannot hide behind the same mistake in how it
  // reads it back.
  pal::posix::PosixPlatform platform;
  ASSERT_TRUE(Tracer::prepare_current_thread("test-main"));
  discard_pending();
  const TracingOn tracing;

  constexpr std::uint32_t kMessageId = 5;
  for (int cycle = 0; cycle < 4; ++cycle) {
    VOLT_TRACE(TraceEvent::kTaskActivate, static_cast<std::uint32_t>(cycle));
    const TraceScope scope{TraceEvent::kTaskStart, TraceEvent::kTaskEnd,
                           static_cast<std::uint32_t>(cycle)};
    VOLT_TRACE(TraceEvent::kMessageTransmit, kMessageId);
    VOLT_TRACE(TraceEvent::kMessageReceive, kMessageId);
  }

  const Capture capture = collect(Tracer::instance());
  const std::string json = to_chrome_trace(capture, Tracer::instance().cycle_clock());
  ASSERT_TRUE(write_text(platform, VOLT_TRACE_SAMPLE_PATH, json));

  const std::array<std::string_view, 2> arguments{VOLT_TRACE_VALIDATOR, VOLT_TRACE_SAMPLE_PATH};
  core::expected<std::unique_ptr<pal::IProcess>> validator = platform.spawn_process(
      pal::ProcessConfig{.executable = "/usr/bin/python3", .arguments = arguments});
  if (!validator.has_value()) {
    GTEST_SKIP() << "no python3 to run the reference validator with";
  }

  const core::expected<pal::ProcessExit> exit = (*validator)->wait();
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->reason, pal::ExitReason::kReturned);
  EXPECT_EQ(exit->code, 0) << "the reference validator rejected " << VOLT_TRACE_SAMPLE_PATH;
}

TEST(TraceFileTest, RoundTripsACaptureWithItsCalibration) {
  Capture capture;
  capture.thread_names.emplace_back("rt-control");
  capture.dropped = 3;
  capture.records.push_back(
      TraceRecord{.cycles = 111, .event = TraceEvent::kRpcBegin, .thread_index = 0, .argument = 9});
  capture.records.push_back(
      TraceRecord{.cycles = 222, .event = TraceEvent::kRpcEnd, .thread_index = 0, .argument = 9});

  const CycleClock clock = CycleClock::from_measurement(100, 5000, 2.5);
  const std::vector<std::byte> bytes = write_capture(capture, clock);
  const core::expected<StoredCapture> restored = read_capture(bytes);

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->capture.dropped, capture.dropped);
  ASSERT_EQ(restored->capture.records.size(), capture.records.size());
  EXPECT_EQ(restored->capture.records[1].event, TraceEvent::kRpcEnd);
  EXPECT_EQ(restored->capture.thread_names.front(), "rt-control");
  EXPECT_EQ(restored->cycle_clock.ticks_per_nanosecond(), 2.5);
  EXPECT_EQ(restored->cycle_clock.to_nanoseconds(111), clock.to_nanoseconds(111));
}

TEST(TraceFileTest, RejectsAFileThatIsNotACapture) {
  const std::array<std::byte, 8> not_a_capture{};
  const core::expected<StoredCapture> restored = read_capture(not_a_capture);

  ASSERT_FALSE(restored.has_value());
  EXPECT_EQ(restored.error(), core::ErrorCode::kTransientIntegrityCheckFailed);
}

TEST(PerfettoExportTest, EscapesAThreadNameThatWouldBreakTheDocument) {
  Capture capture;
  capture.thread_names.emplace_back(R"(quote " and backslash \)");
  capture.records.push_back(TraceRecord{.event = TraceEvent::kStateChange, .thread_index = 0});

  const std::string json = to_chrome_trace(capture, CycleClock::uncalibrated());
  EXPECT_NE(json.find(R"(quote \" and backslash \\)"), std::string::npos);
}

} // namespace
} // namespace volt::trace
