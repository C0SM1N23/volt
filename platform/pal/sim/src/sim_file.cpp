#include "sim_file.hpp"

#include <algorithm>

namespace volt::pal::sim {

core::expected<std::size_t> SimFile::read(std::span<std::byte> buffer) noexcept {
  if (mode_ != FileMode::kRead) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  const std::size_t remaining = content_->size() - std::min(offset_, content_->size());
  const std::size_t taken = std::min(buffer.size(), remaining);
  std::copy_n(content_->begin() + static_cast<std::ptrdiff_t>(offset_), taken, buffer.begin());
  offset_ += taken;
  return taken;
}

core::expected<std::size_t> SimFile::write(std::span<const std::byte> payload) noexcept {
  if (mode_ == FileMode::kRead) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  if (mode_ == FileMode::kAppend) {
    // Append means every write lands at the end, even if something else grew
    // the file since the last one.
    offset_ = content_->size();
  }
  const std::size_t end = offset_ + payload.size();
  if (end > content_->size()) {
    content_->resize(end, std::byte{0});
  }
  std::copy(payload.begin(), payload.end(),
            content_->begin() + static_cast<std::ptrdiff_t>(offset_));
  offset_ = end;
  world_->record("file.write", static_cast<std::uint64_t>(payload.size()));
  return payload.size();
}

core::expected<void> SimFile::flush() noexcept {
  // Nothing is buffered on the way to the world's storage, so a flush has
  // nothing to push; it still succeeds, because the postcondition the callers
  // rely on already holds.
  return {};
}

core::expected<std::uint64_t> SimFile::size() const noexcept {
  return static_cast<std::uint64_t>(content_->size());
}

} // namespace volt::pal::sim
