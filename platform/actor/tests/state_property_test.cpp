#include "volt/actor/state_reader.hpp"
#include "volt/actor/state_writer.hpp"

#include "volt/core/hash.hpp"

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace volt {
namespace {

// P11 requires ten thousand generated state round trips. Lowering this reduces
// the required evidence; raising it changes test duration rather than behavior.
constexpr int kPropertyCases = 10'000;
// State fields below need 29 payload bytes plus the versioned header. The
// margin keeps the property focused on round-trip correctness, not exhaustion.
constexpr std::size_t kRecordCapacityBytes = 64;
// Fixed seed required by AGENTS.md 8.5. RapidCheck prints it with the shrunk
// counterexample so every failure can be replayed exactly.
constexpr std::uint64_t kPropertySeed = 0x6E73'884B'D1A2'0F95ULL;
// Schema version of the generated state record. Changing its layout requires
// changing this value so an old record is rejected instead of misread.
constexpr StateVersion kGeneratedStateVersion{7};
// Six calibration bytes represent the bounded opaque state fragment used by
// this property. Changing it changes the generated schema and its record size.
constexpr std::size_t kCalibrationBytes = 6;
// The P11 format header is four marker bytes plus a 16-bit schema version.
// These test sizes straddle that fixed boundary to exercise both outcomes.
constexpr std::size_t kStateHeaderBytes = 6;
constexpr std::size_t kTruncatedStateHeaderBytes = kStateHeaderBytes - 1U;

struct GeneratedState {
  std::uint8_t mode = 0;
  std::uint16_t flags = 0;
  std::uint32_t sequence = 0;
  std::uint64_t accumulator = 0;
  std::int64_t signed_offset = 0;
  std::array<std::byte, kCalibrationBytes> calibration{};

  [[nodiscard]] bool operator==(const GeneratedState &) const noexcept = default;
};

[[nodiscard]] expected<std::span<const std::byte>>
serialize_state(const GeneratedState &state, std::span<std::byte> storage) noexcept {
  expected<StateWriter> writer = StateWriter::create(storage, kGeneratedStateVersion);
  if (!writer.has_value()) {
    return std::unexpected{writer.error()};
  }
  VOLT_TRY(writer->write_u8(state.mode));
  VOLT_TRY(writer->write_u16(state.flags));
  VOLT_TRY(writer->write_u32(state.sequence));
  VOLT_TRY(writer->write_u64(state.accumulator));
  VOLT_TRY(writer->write_i64(state.signed_offset));
  VOLT_TRY(writer->write_bytes(state.calibration));
  return writer->finish();
}

[[nodiscard]] expected<GeneratedState>
deserialize_state(std::span<const std::byte> record) noexcept {
  expected<StateReader> reader = StateReader::create(record, kGeneratedStateVersion);
  if (!reader.has_value()) {
    return std::unexpected{reader.error()};
  }

  const expected<std::uint8_t> mode = reader->read_u8();
  const expected<std::uint16_t> flags = reader->read_u16();
  const expected<std::uint32_t> sequence = reader->read_u32();
  const expected<std::uint64_t> accumulator = reader->read_u64();
  const expected<std::int64_t> signed_offset = reader->read_i64();
  const expected<std::span<const std::byte>> calibration =
      reader->read_bytes(GeneratedState{}.calibration.size());
  if (!mode || !flags || !sequence || !accumulator || !signed_offset || !calibration ||
      !reader->finished()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  GeneratedState result{.mode = *mode,
                        .flags = *flags,
                        .sequence = *sequence,
                        .accumulator = *accumulator,
                        .signed_offset = *signed_offset};
  std::ranges::copy(*calibration, result.calibration.begin());
  return result;
}

[[nodiscard]] std::array<std::byte, kCalibrationBytes>
calibration_from(std::uint64_t bits) noexcept {
  const std::array<std::byte, sizeof(bits)> bytes =
      std::bit_cast<std::array<std::byte, sizeof(bits)>>(bits);
  std::array<std::byte, kCalibrationBytes> result{};
  std::ranges::copy_n(bytes.begin(), result.size(), result.begin());
  return result;
}

template <typename Property>
void expect_property(Property property, const std::string &identifier) {
  rc::detail::TestMetadata metadata{.id = identifier, .description = identifier};
  rc::detail::TestParams parameters{};
  parameters.seed = kPropertySeed;
  parameters.maxSuccess = kPropertyCases;

  const rc::detail::TestResult result =
      rc::detail::checkTestable(std::move(property), metadata, parameters);
  std::ostringstream diagnostic;
  rc::detail::printResultMessage(result, diagnostic);
  ASSERT_TRUE(result.is<rc::detail::SuccessResult>()) << "seed=" << kPropertySeed << '\n'
                                                      << diagnostic.str();
}

// @verifies REQ-PLT-030
TEST(StateCodecPropertyTest, RestoresEqualStateAndHashForTenThousandGeneratedStates) {
  expect_property(
      [](std::uint8_t mode, std::uint16_t flags, std::uint32_t sequence, std::uint64_t accumulator,
         std::int64_t signed_offset, std::uint64_t calibration_bits) {
        const GeneratedState original{.mode = mode,
                                      .flags = flags,
                                      .sequence = sequence,
                                      .accumulator = accumulator,
                                      .signed_offset = signed_offset,
                                      .calibration = calibration_from(calibration_bits)};
        std::array<std::byte, kRecordCapacityBytes> original_storage{};
        const expected<std::span<const std::byte>> original_record =
            serialize_state(original, original_storage);
        RC_ASSERT(original_record.has_value());

        const expected<GeneratedState> restored = deserialize_state(*original_record);
        RC_ASSERT(restored.has_value());
        RC_ASSERT(*restored == original);

        std::array<std::byte, kRecordCapacityBytes> restored_storage{};
        const expected<std::span<const std::byte>> restored_record =
            serialize_state(*restored, restored_storage);
        RC_ASSERT(restored_record.has_value());
        RC_ASSERT(core::xxhash64(*original_record) == core::xxhash64(*restored_record));
      },
      "actor-state-round-trip");
}

TEST(StateCodecTest, WritesStableLittleEndianHeaderAndAllScalarWidths) {
  std::array<std::byte, kRecordCapacityBytes> storage{};
  expected<StateWriter> writer = StateWriter::create(storage, kGeneratedStateVersion);
  ASSERT_TRUE(writer.has_value());
  EXPECT_EQ(writer->version(), kGeneratedStateVersion);
  ASSERT_TRUE(writer->write_u8(0x12U).has_value());
  ASSERT_TRUE(writer->write_u16(0x3456U).has_value());
  ASSERT_TRUE(writer->write_u32(0x789A'BCDEU).has_value());
  ASSERT_TRUE(writer->write_u64(0x0123'4567'89AB'CDEFULL).has_value());
  ASSERT_TRUE(writer->write_i64(-2).has_value());

  const std::span<const std::byte> record = writer->finish();
  ASSERT_GE(record.size(), 13U);
  EXPECT_EQ(record[0], std::byte{'V'});
  EXPECT_EQ(record[1], std::byte{'S'});
  EXPECT_EQ(record[2], std::byte{'T'});
  EXPECT_EQ(record[3], std::byte{'A'});
  EXPECT_EQ(record[4], std::byte{7});
  EXPECT_EQ(record[6], std::byte{0x12});
  EXPECT_EQ(record[7], std::byte{0x56});
  EXPECT_EQ(record[8], std::byte{0x34});
  EXPECT_EQ(writer->remaining_bytes(), storage.size() - record.size());
}

TEST(StateCodecTest, RejectsShortHeaderWrongMarkerAndUnsupportedVersion) {
  std::array<std::byte, kTruncatedStateHeaderBytes> short_record{};
  EXPECT_EQ(StateReader::create(short_record, kGeneratedStateVersion).error(),
            core::ErrorCode::kInternalBufferTooSmall);

  std::array<std::byte, kRecordCapacityBytes> storage{};
  expected<StateWriter> writer = StateWriter::create(storage, kGeneratedStateVersion);
  ASSERT_TRUE(writer.has_value());
  const std::span<const std::byte> valid = writer->finish();
  storage[0] = std::byte{0};
  EXPECT_EQ(StateReader::create(valid, kGeneratedStateVersion).error(),
            core::ErrorCode::kTransientIntegrityCheckFailed);
  storage[0] = std::byte{'V'};
  EXPECT_EQ(StateReader::create(valid, StateVersion{8}).error(),
            core::ErrorCode::kExternalUnsupportedRequest);
}

TEST(StateCodecTest, ReportsBufferExhaustionWithoutAdvancing) {
  std::array<std::byte, kStateHeaderBytes> header_only{};
  expected<StateWriter> writer = StateWriter::create(header_only, kGeneratedStateVersion);
  ASSERT_TRUE(writer.has_value());
  EXPECT_EQ(writer->write_u8(1).error(), core::ErrorCode::kInternalBufferTooSmall);
  EXPECT_EQ(writer->write_bytes(std::array<std::byte, 1>{}).error(),
            core::ErrorCode::kInternalBufferTooSmall);
  EXPECT_FALSE(StateWriter::create(std::span<std::byte>{}, kGeneratedStateVersion).has_value());

  expected<StateReader> reader = StateReader::create(writer->finish(), kGeneratedStateVersion);
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(reader->version(), kGeneratedStateVersion);
  EXPECT_EQ(reader->remaining_bytes(), 0U);
  EXPECT_TRUE(reader->finished());
  EXPECT_EQ(reader->read_u8().error(), core::ErrorCode::kInternalBufferTooSmall);
  EXPECT_EQ(reader->read_bytes(1).error(), core::ErrorCode::kInternalBufferTooSmall);
}

} // namespace
} // namespace volt
