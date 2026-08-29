#include "volt/actor/state_reader.hpp"

#include "state_format.hpp"

#include "volt/core/endian.hpp"

#include <bit>
#include <cstring>

namespace volt {

expected<StateReader> StateReader::create(std::span<const std::byte> record,
                                          StateVersion expected_version) noexcept {
  if (record.size() < actor::detail::kStateHeaderBytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  StateReader reader{record, expected_version};
  const expected<std::uint32_t> magic = reader.read_u32();
  VOLT_ASSERT(magic.has_value(), "validated state header lost its magic bytes");
  if (*magic != actor::detail::kStateMagic) {
    return std::unexpected{core::ErrorCode::kTransientIntegrityCheckFailed};
  }

  const expected<std::uint16_t> version = reader.read_u16();
  VOLT_ASSERT(version.has_value(), "validated state header lost its version bytes");
  if (*version != expected_version.value()) {
    return std::unexpected{core::ErrorCode::kExternalUnsupportedRequest};
  }
  return reader;
}

StateReader::StateReader(std::span<const std::byte> record, StateVersion version) noexcept
    : record_{record}, version_{version} {}

expected<std::uint8_t> StateReader::read_u8() noexcept {
  return read_integral(sizeof(std::uint8_t)).transform([](std::uint64_t value) {
    return static_cast<std::uint8_t>(value);
  });
}

expected<std::uint16_t> StateReader::read_u16() noexcept {
  return read_integral(sizeof(std::uint16_t)).transform([](std::uint64_t value) {
    return static_cast<std::uint16_t>(value);
  });
}

expected<std::uint32_t> StateReader::read_u32() noexcept {
  return read_integral(sizeof(std::uint32_t)).transform([](std::uint64_t value) {
    return static_cast<std::uint32_t>(value);
  });
}

expected<std::uint64_t> StateReader::read_u64() noexcept {
  return read_integral(sizeof(std::uint64_t));
}

expected<std::int64_t> StateReader::read_i64() noexcept {
  return read_integral(sizeof(std::int64_t)).transform([](std::uint64_t value) {
    return std::bit_cast<std::int64_t>(value);
  });
}

expected<std::span<const std::byte>> StateReader::read_bytes(std::size_t count_bytes) noexcept {
  if (count_bytes > remaining_bytes()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  const std::span<const std::byte> result = record_.subspan(offset_bytes_, count_bytes);
  offset_bytes_ += count_bytes;
  return result;
}

bool StateReader::finished() const noexcept { return offset_bytes_ == record_.size(); }

std::size_t StateReader::remaining_bytes() const noexcept { return record_.size() - offset_bytes_; }

StateVersion StateReader::version() const noexcept { return version_; }

expected<std::uint64_t> StateReader::read_integral(std::size_t width_bytes) noexcept {
  if (width_bytes > remaining_bytes()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  std::uint64_t decoded = 0;
  if (width_bytes == sizeof(std::uint8_t)) {
    std::uint8_t encoded = 0;
    std::memcpy(&encoded, record_.data() + offset_bytes_, sizeof(encoded));
    decoded = encoded;
  } else if (width_bytes == sizeof(std::uint16_t)) {
    std::uint16_t encoded = 0;
    std::memcpy(&encoded, record_.data() + offset_bytes_, sizeof(encoded));
    decoded = core::from_little_endian(encoded);
  } else if (width_bytes == sizeof(std::uint32_t)) {
    std::uint32_t encoded = 0;
    std::memcpy(&encoded, record_.data() + offset_bytes_, sizeof(encoded));
    decoded = core::from_little_endian(encoded);
  } else {
    VOLT_ASSERT(width_bytes == sizeof(std::uint64_t), "state integer width is unsupported");
    std::uint64_t encoded = 0;
    std::memcpy(&encoded, record_.data() + offset_bytes_, sizeof(encoded));
    decoded = core::from_little_endian(encoded);
  }
  offset_bytes_ += width_bytes;
  return decoded;
}

} // namespace volt
