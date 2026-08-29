#include "volt/log/rotating_file_sink.hpp"

#include "volt/log/log_file_header.hpp"

#include <utility>

namespace volt::log {

RotatingFileSink::RotatingFileSink(pal::IPlatform &platform, std::string_view directory,
                                   std::string_view base_name, RotationPolicy policy)
    : platform_{&platform}, directory_{directory}, base_name_{base_name}, policy_{policy} {
  // A sink that could not open its first file is not a failure worth refusing
  // to start over: it reports a full disk from the outset, and the system runs
  // without a log rather than not at all.
  const core::expected<void> opened = open_next_file();
  static_cast<void>(opened.has_value());
}

std::string RotatingFileSink::path_for(unsigned index) const {
  return directory_ + "/" + base_name_ + "." + std::to_string(index) + ".vlog";
}

core::expected<void> RotatingFileSink::open_next_file() noexcept {
  // Indices cycle through the kept files, so the oldest is the one overwritten
  // and the total on disk stays bounded without deleting anything.
  const unsigned index = next_index_ % policy_.max_files;
  next_index_ = index + 1;

  std::string path = path_for(index);
  core::expected<std::unique_ptr<pal::IFile>> opened =
      platform_->open_file(path, pal::FileMode::kWrite);
  if (!opened.has_value()) {
    disk_is_full_ = true;
    return std::unexpected{opened.error()};
  }

  const std::vector<std::byte> header = build_log_file_header(registered_formats());
  const core::expected<std::size_t> written = (*opened)->write(header);
  if (!written.has_value()) {
    disk_is_full_ = true;
    return std::unexpected{written.error()};
  }

  file_ = std::move(*opened);
  current_path_ = std::move(path);
  bytes_in_file_ = *written;
  return {};
}

core::expected<void> RotatingFileSink::write(std::span<const std::byte> record) noexcept {
  if (disk_is_full_ || file_ == nullptr) {
    discarded_records_ += 1;
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }

  if (bytes_in_file_ + record.size() > policy_.max_file_bytes) {
    const core::expected<void> rotated = open_next_file();
    if (!rotated.has_value()) {
      discarded_records_ += 1;
      return std::unexpected{rotated.error()};
    }
    rotations_ += 1;
  }

  const core::expected<std::size_t> written = file_->write(record);
  if (!written.has_value() || *written != record.size()) {
    // A short write means the filesystem has no room. Recording stops here and
    // the files already written stay, which is the policy of SPEC 42.3.
    disk_is_full_ = true;
    discarded_records_ += 1;
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }
  bytes_in_file_ += *written;
  return {};
}

core::expected<void> RotatingFileSink::flush() noexcept {
  if (file_ == nullptr) {
    return std::unexpected{core::ErrorCode::kResourceUnavailable};
  }
  return file_->flush();
}

} // namespace volt::log
