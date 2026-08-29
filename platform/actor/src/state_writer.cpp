#include "volt/actor/state_writer.hpp"

#include "state_format.hpp"

#include "volt/core/endian.hpp"

#include <bit>
#include <cstring>

namespace volt {

expected<StateWriter> StateWriter::create(std::span<std::byte> storage,
                                          StateVersion version) noexcept {
  if (storage.size() < actor::detail::kStateHeaderBytes) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }

  StateWriter writer{storage, version};
  VOLT_TRY(writer.write_u32(actor::detail::kStateMagic));
  VOLT_TRY(writer.write_u16(version.value()));
  return writer;
}

StateWriter::StateWriter(std::span<std::byte> storage, StateVersion version) noexcept
    : storage_{storage}, version_{version} {}

expected<void> StateWriter::write_u8(std::uint8_t value) noexcept {
  return append_integral(value, sizeof(value));
}

expected<void> StateWriter::write_u16(std::uint16_t value) noexcept {
  return append_integral(value, sizeof(value));
}

expected<void> StateWriter::write_u32(std::uint32_t value) noexcept {
  return append_integral(value, sizeof(value));
}

expected<void> StateWriter::write_u64(std::uint64_t value) noexcept {
  return append_integral(value, sizeof(value));
}

expected<void> StateWriter::write_i64(std::int64_t value) noexcept {
  return write_u64(std::bit_cast<std::uint64_t>(value));
}

expected<void> StateWriter::write_bytes(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() > remaining_bytes()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  if (!bytes.empty()) {
    std::memcpy(storage_.data() + offset_bytes_, bytes.data(), bytes.size());
  }
  offset_bytes_ += bytes.size();
  return {};
}

std::span<const std::byte> StateWriter::finish() const noexcept {
  return std::span<const std::byte>{storage_.data(), offset_bytes_};
}

std::size_t StateWriter::remaining_bytes() const noexcept {
  return storage_.size() - offset_bytes_;
}

StateVersion StateWriter::version() const noexcept { return version_; }

expected<void> StateWriter::append_integral(std::uint64_t value, std::size_t width_bytes) noexcept {
  if (width_bytes > remaining_bytes()) {
    return std::unexpected{core::ErrorCode::kInternalBufferTooSmall};
  }
  if (width_bytes == sizeof(std::uint8_t)) {
    const std::uint8_t encoded = static_cast<std::uint8_t>(value);
    std::memcpy(storage_.data() + offset_bytes_, &encoded, sizeof(encoded));
  } else if (width_bytes == sizeof(std::uint16_t)) {
    const std::uint16_t encoded = core::to_little_endian(static_cast<std::uint16_t>(value));
    std::memcpy(storage_.data() + offset_bytes_, &encoded, sizeof(encoded));
  } else if (width_bytes == sizeof(std::uint32_t)) {
    const std::uint32_t encoded = core::to_little_endian(static_cast<std::uint32_t>(value));
    std::memcpy(storage_.data() + offset_bytes_, &encoded, sizeof(encoded));
  } else {
    VOLT_ASSERT(width_bytes == sizeof(std::uint64_t), "state integer width is unsupported");
    const std::uint64_t encoded = core::to_little_endian(value);
    std::memcpy(storage_.data() + offset_bytes_, &encoded, sizeof(encoded));
  }
  offset_bytes_ += width_bytes;
  return {};
}

} // namespace volt
