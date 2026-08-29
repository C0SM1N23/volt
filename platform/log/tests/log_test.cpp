#include "volt/log/log_drain.hpp"
#include "volt/log/log_file_header.hpp"
#include "volt/log/logger.hpp"
#include "volt/log/record_reader.hpp"
#include "volt/log/rotating_file_sink.hpp"

#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace volt::log {
namespace {

/// Keeps every record in memory so a test can look at what was drained.
class CollectingSink final : public ILogSink {
public:
  [[nodiscard]] core::expected<void> write(std::span<const std::byte> record) noexcept override {
    records_.emplace_back(record.begin(), record.end());
    return {};
  }

  [[nodiscard]] core::expected<void> flush() noexcept override { return {}; }

  [[nodiscard]] const std::vector<std::vector<std::byte>> &records() const noexcept {
    return records_;
  }

private:
  std::vector<std::vector<std::byte>> records_;
};

/// Refuses everything, the way a sink on a full filesystem does.
class FullDiskSink final : public ILogSink {
public:
  [[nodiscard]] core::expected<void> write(std::span<const std::byte> record) noexcept override {
    static_cast<void>(record);
    refused_ += 1;
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }

  [[nodiscard]] core::expected<void> flush() noexcept override { return {}; }

  [[nodiscard]] std::uint64_t refused() const noexcept { return refused_; }

private:
  std::uint64_t refused_ = 0;
};

/// Drains whatever the calling thread has queued and returns the records.
[[nodiscard]] std::vector<std::vector<std::byte>> drain(pal::IPlatform &platform,
                                                        CollectingSink &sink) {
  LogDrain drain_loop{Logger::instance(), sink, platform};
  static_cast<void>(drain_loop.drain_once());
  return sink.records();
}

[[nodiscard]] std::unique_ptr<pal::sim::SimPlatform> make_platform() {
  auto platform = std::make_unique<pal::sim::SimPlatform>(pal::sim::SimConfig{.seed = 1});
  Logger::instance().set_clock(platform->clock());
  Logger::instance().set_level_for_all(Level::kTrace);
  return platform;
}

TEST(RecordTest, RoundTripsEveryArgumentKind) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  constexpr std::int64_t kSigned = -1234;
  constexpr std::uint64_t kUnsigned = 5678;
  constexpr double kDouble = 2.5;
  VOLT_LOG_INFO(Module::kCore, "signed {} unsigned {} double {} text {} flag {}", kSigned,
                kUnsigned, kDouble, std::string_view{"hello"}, true);

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());
  EXPECT_EQ(reader.level(), Level::kInfo);
  EXPECT_EQ(reader.module(), Module::kCore);
  ASSERT_EQ(reader.argument_count(), 5U);

  EXPECT_EQ(std::get<std::int64_t>(*reader.next_argument()), kSigned);
  EXPECT_EQ(std::get<std::uint64_t>(*reader.next_argument()), kUnsigned);
  EXPECT_EQ(std::get<double>(*reader.next_argument()), kDouble);
  EXPECT_EQ(std::get<std::string_view>(*reader.next_argument()), "hello");
  EXPECT_EQ(std::get<std::uint64_t>(*reader.next_argument()), 1U);
}

TEST(RecordTest, CarriesTheFormatIdRatherThanTheText) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  VOLT_LOG_WARN(Module::kSafety, "brake pressure {} out of range", 42);

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());

  const std::span<const FormatEntry> formats = registered_formats();
  const auto match = std::ranges::find_if(
      formats, [&reader](const FormatEntry &entry) { return entry.id == reader.format_id(); });
  ASSERT_NE(match, formats.end());
  EXPECT_STREQ(match->format, "brake pressure {} out of range");
}

TEST(RecordTest, TruncatesTextLongerThanTheCap) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  const std::string long_text(kMaxTextArgumentBytes * 2, 'x');
  VOLT_LOG_INFO(Module::kCore, "text {}", std::string_view{long_text});

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());
  EXPECT_EQ(std::get<std::string_view>(*reader.next_argument()).size(), kMaxTextArgumentBytes);
}

TEST(LoggerTest, FilterKeepsRecordsAtOrAboveTheModuleLevel) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  Logger::instance().set_level(Module::kCore, Level::kWarn);
  VOLT_LOG_DEBUG(Module::kCore, "dropped by the filter");
  VOLT_LOG_ERROR(Module::kCore, "kept by the filter");
  Logger::instance().set_level_for_all(Level::kTrace);

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());
  EXPECT_EQ(reader.level(), Level::kError);
}

TEST(LoggerTest, FilterIsPerModule) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  Logger::instance().set_level(Module::kCore, kSilent);
  VOLT_LOG_ERROR(Module::kCore, "silenced module");
  VOLT_LOG_ERROR(Module::kSafety, "other module still speaks");
  Logger::instance().set_level_for_all(Level::kTrace);

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());
  EXPECT_EQ(reader.module(), Module::kSafety);
}

TEST(LoggerTest, StampsRecordsWithTheInjectedClock) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  CollectingSink sink;

  ASSERT_TRUE(platform->clock().sleep_for(core::Duration::from_ms(5)).has_value());
  const std::int64_t before = platform->clock().monotonic().ns_since_epoch();
  VOLT_LOG_INFO(Module::kTime, "stamped");

  const std::vector<std::vector<std::byte>> records = drain(*platform, sink);
  ASSERT_EQ(records.size(), 1U);

  RecordReader reader{records.front()};
  ASSERT_TRUE(reader.parse_header().has_value());
  EXPECT_EQ(reader.timestamp_ns(), before);
}

TEST(LogDrainTest, ReportsRecordsTheSinkRefused) {
  const std::unique_ptr<pal::sim::SimPlatform> platform = make_platform();
  FullDiskSink sink;
  LogDrain drain_loop{Logger::instance(), sink, *platform};

  VOLT_LOG_ERROR(Module::kCore, "written to a full disk");
  EXPECT_EQ(drain_loop.drain_once(), 0U);
  EXPECT_EQ(drain_loop.records_refused_by_sink(), 1U);
  EXPECT_EQ(sink.refused(), 1U);
}

TEST(LogFileHeaderTest, RoundTripsTheFormatTable) {
  const std::vector<std::byte> header = build_log_file_header(registered_formats());
  std::size_t consumed = 0;
  const core::expected<std::vector<DecodedFormat>> formats =
      parse_log_file_header(header, consumed);

  ASSERT_TRUE(formats.has_value());
  EXPECT_EQ(consumed, header.size());
  EXPECT_EQ(formats->size(), registered_formats().size());
  EXPECT_GT(formats->size(), 0U);
}

TEST(LogFileHeaderTest, RejectsAFileThatIsNotALog) {
  const std::array<std::byte, 8> not_a_log{};
  std::size_t consumed = 0;
  const core::expected<std::vector<DecodedFormat>> formats =
      parse_log_file_header(not_a_log, consumed);

  ASSERT_FALSE(formats.has_value());
  EXPECT_EQ(formats.error(), core::ErrorCode::kTransientIntegrityCheckFailed);
}

TEST(LogFileHeaderTest, RejectsAHeaderCutShort) {
  const std::vector<std::byte> header = build_log_file_header(registered_formats());
  std::size_t consumed = 0;
  const core::expected<std::vector<DecodedFormat>> formats =
      parse_log_file_header(std::span<const std::byte>{header}.first(header.size() / 2), consumed);

  ASSERT_FALSE(formats.has_value());
  EXPECT_EQ(formats.error(), core::ErrorCode::kInternalBufferTooSmall);
}

} // namespace
} // namespace volt::log
