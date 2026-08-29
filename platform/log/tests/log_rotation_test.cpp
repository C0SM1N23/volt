#include "volt/log/log_drain.hpp"
#include "volt/log/log_file_header.hpp"
#include "volt/log/logger.hpp"
#include "volt/log/record_reader.hpp"
#include "volt/log/rotating_file_sink.hpp"

#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace volt::log {
namespace {

// Small enough that a handful of records fills a file, so rotation is reached
// without writing megabytes; the policy under test is the behaviour, not the
// hundred megabytes SPEC 42.3 uses in the field.
constexpr std::uint64_t kSmallFileBytes = 4096;
constexpr unsigned kKeptFiles = 3;
constexpr int kRecordsWritten = 400;

constexpr std::string_view kDirectory = "/sim/log";
constexpr std::string_view kBaseName = "volt";

[[nodiscard]] std::unique_ptr<pal::sim::SimPlatform> make_platform(std::uint64_t capacity) {
  auto platform = std::make_unique<pal::sim::SimPlatform>(
      pal::sim::SimConfig{.seed = 1, .file_system_capacity_bytes = capacity});
  Logger::instance().set_clock(platform->clock());
  Logger::instance().set_level_for_all(Level::kTrace);
  return platform;
}

/// Logs `count` records and drains them into `sink`.
void produce_and_drain(pal::IPlatform &platform, ILogSink &sink, int count) {
  LogDrain drain{Logger::instance(), sink, platform};
  for (int index = 0; index < count; ++index) {
    VOLT_LOG_INFO(Module::kCore, "rotation record {}", index);
    static_cast<void>(drain.drain_once());
  }
}

TEST(RotationTest, MovesToANewFileOnceOneFills) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(0);
  RotatingFileSink sink{*platform, kDirectory, kBaseName,
                        RotationPolicy{.max_file_bytes = kSmallFileBytes, .max_files = kKeptFiles}};

  produce_and_drain(*platform, sink, kRecordsWritten);

  EXPECT_GT(sink.rotations(), 0U);
  EXPECT_FALSE(sink.stopped_on_full_disk());
}

TEST(RotationTest, ReusesTheOldestFileRatherThanGrowingWithoutBound) {
  // The point of a rotation count is that a node logging for a month occupies
  // the same space as one that started yesterday.
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(0);
  RotatingFileSink sink{*platform, kDirectory, kBaseName,
                        RotationPolicy{.max_file_bytes = kSmallFileBytes, .max_files = kKeptFiles}};

  produce_and_drain(*platform, sink, kRecordsWritten);

  // Every path the sink can ever use is one of the kept indices, so the file
  // count is capped no matter how long the run lasts.
  EXPECT_GT(sink.rotations(), kKeptFiles);
  const std::string current{sink.current_path()};
  bool matches_a_kept_index = false;
  for (unsigned index = 0; index < kKeptFiles; ++index) {
    const std::string candidate = std::string{kDirectory} + "/" + std::string{kBaseName} + "." +
                                  std::to_string(index) + ".vlog";
    matches_a_kept_index = matches_a_kept_index || current == candidate;
  }
  EXPECT_TRUE(matches_a_kept_index) << "current path " << current << " is outside the kept set";
}

TEST(RotationTest, EveryFileStartsWithItsOwnFormatTable) {
  // A rotated file has to be decodable on its own: the one that survives a
  // crash is rarely the first.
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(0);
  RotatingFileSink sink{*platform, kDirectory, kBaseName,
                        RotationPolicy{.max_file_bytes = kSmallFileBytes, .max_files = kKeptFiles}};
  produce_and_drain(*platform, sink, kRecordsWritten);

  core::expected<std::unique_ptr<pal::IFile>> file =
      platform->open_file(sink.current_path(), pal::FileMode::kRead);
  ASSERT_TRUE(file.has_value());

  std::vector<std::byte> content((*file)->size().value());
  ASSERT_TRUE((*file)->read(content).has_value());

  std::size_t consumed = 0;
  const core::expected<std::vector<DecodedFormat>> formats =
      parse_log_file_header(content, consumed);
  ASSERT_TRUE(formats.has_value());
  EXPECT_GT(formats->size(), 0U);
}

TEST(RotationTest, StopsRecordingAndCountsTheLossWhenTheDiskFills) {
  // The policy of SPEC 42.3: the system keeps running, recording stops, the
  // loss is counted, and what was already written stays readable. A vehicle
  // that shut down over a full log partition would be the worse failure.
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(kSmallFileBytes);
  RotatingFileSink sink{
      *platform, kDirectory, kBaseName,
      RotationPolicy{.max_file_bytes = kSmallFileBytes * 100, .max_files = kKeptFiles}};

  produce_and_drain(*platform, sink, kRecordsWritten);

  EXPECT_TRUE(sink.stopped_on_full_disk());
  EXPECT_GT(sink.discarded_records(), 0U);
}

TEST(RotationTest, KeepsWhatWasAlreadyWrittenAfterTheDiskFills) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(kSmallFileBytes);
  RotatingFileSink sink{
      *platform, kDirectory, kBaseName,
      RotationPolicy{.max_file_bytes = kSmallFileBytes * 100, .max_files = kKeptFiles}};
  produce_and_drain(*platform, sink, kRecordsWritten);
  ASSERT_TRUE(sink.stopped_on_full_disk());

  core::expected<std::unique_ptr<pal::IFile>> file =
      platform->open_file(sink.current_path(), pal::FileMode::kRead);
  ASSERT_TRUE(file.has_value());

  std::vector<std::byte> content((*file)->size().value());
  ASSERT_TRUE((*file)->read(content).has_value());

  std::size_t consumed = 0;
  const core::expected<std::vector<DecodedFormat>> formats =
      parse_log_file_header(content, consumed);
  ASSERT_TRUE(formats.has_value()) << "the file left behind is no longer readable";
}

TEST(RotationTest, RoundTripsTenThousandRecordsThroughAFile) {
  constexpr int kRoundTripRecords = 10'000;
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform(0);
  RotatingFileSink sink{
      *platform, kDirectory, kBaseName,
      RotationPolicy{.max_file_bytes = 64ULL * 1024ULL * 1024ULL, .max_files = kKeptFiles}};

  {
    LogDrain drain{Logger::instance(), sink, *platform};
    for (int index = 0; index < kRoundTripRecords; ++index) {
      VOLT_LOG_INFO(Module::kService, "round trip {} value {} name {}", index,
                    static_cast<double>(index) / 2.0, std::string_view{"payload"});
      static_cast<void>(drain.drain_once());
    }
    ASSERT_TRUE(sink.flush().has_value());
    EXPECT_EQ(drain.records_refused_by_sink(), 0U);
  }

  core::expected<std::unique_ptr<pal::IFile>> file =
      platform->open_file(sink.current_path(), pal::FileMode::kRead);
  ASSERT_TRUE(file.has_value());
  std::vector<std::byte> content((*file)->size().value());
  ASSERT_TRUE((*file)->read(content).has_value());

  std::size_t offset = 0;
  const core::expected<std::vector<DecodedFormat>> formats = parse_log_file_header(content, offset);
  ASSERT_TRUE(formats.has_value());

  int decoded = 0;
  while (offset < content.size()) {
    RecordReader reader{std::span<const std::byte>{content}.subspan(offset)};
    ASSERT_TRUE(reader.parse_header().has_value()) << "record " << decoded << " is malformed";
    ASSERT_EQ(reader.argument_count(), 3U);

    const core::expected<Argument> index = reader.next_argument();
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*index), decoded);
    EXPECT_TRUE(reader.next_argument().has_value());
    const core::expected<Argument> name = reader.next_argument();
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(std::get<std::string_view>(*name), "payload");

    offset += reader.total_bytes();
    decoded += 1;
  }
  EXPECT_EQ(decoded, kRoundTripRecords);
}

} // namespace
} // namespace volt::log
