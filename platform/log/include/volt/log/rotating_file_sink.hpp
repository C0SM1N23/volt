#pragma once

#include "volt/log/log_sink.hpp"
#include "volt/pal/platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace volt::log {

/// Bytes in a megabyte, so the size below reads as the unit SPEC 42.3 states.
inline constexpr std::uint64_t kBytesPerMegabyte = 1024ULL * 1024ULL;

/// Size one log file reaches before the sink moves to the next, from SPEC 42.3.
///
/// It bounds how long a single file takes to copy off a vehicle. Raising it
/// makes each capture slower to retrieve; lowering it spreads one incident
/// across more files.
inline constexpr std::uint64_t kDefaultMaxFileBytes = 100ULL * kBytesPerMegabyte;

/// Files kept before the oldest is written over, from SPEC 42.3.
///
/// Together with the size above this bounds the total on disk, so a node that
/// has been logging for a month occupies what one that started yesterday does.
inline constexpr unsigned kDefaultKeptFiles = 5;

/// How much log a node keeps on disk.
///
/// A hundred megabytes across five files, from SPEC 42.3. The size bounds how
/// long a single file takes to copy off a vehicle; the count bounds the total,
/// so a node that has been logging for a month occupies the same space as one
/// that started yesterday.
struct RotationPolicy {
  std::uint64_t max_file_bytes = kDefaultMaxFileBytes;
  unsigned max_files = kDefaultKeptFiles;
};

/// Writes records to a file, rotating by size and surviving a full disk.
///
/// The disk-full rule of SPEC 42.3 is the important part: recording stops, the
/// count of what was lost keeps rising, and the files already written stay.
/// The system does not stop, because a vehicle that shut down over a full log
/// partition would be a far worse failure than a gap in the log.
class RotatingFileSink final : public ILogSink {
public:
  /// Opens the first file and writes its format table.
  ///
  /// @pre `platform` outlives this sink
  RotatingFileSink(pal::IPlatform &platform, std::string_view directory, std::string_view base_name,
                   RotationPolicy policy);

  [[nodiscard]] core::expected<void> write(std::span<const std::byte> record) noexcept override;
  [[nodiscard]] core::expected<void> flush() noexcept override;

  /// Reports whether writing has stopped because there was no room.
  [[nodiscard]] bool stopped_on_full_disk() const noexcept { return disk_is_full_; }

  /// Returns how many records were thrown away after the disk filled up.
  [[nodiscard]] std::uint64_t discarded_records() const noexcept { return discarded_records_; }

  /// Returns how many times the sink has moved to a new file.
  [[nodiscard]] unsigned rotations() const noexcept { return rotations_; }

  /// Returns the path currently being written.
  [[nodiscard]] std::string_view current_path() const noexcept { return current_path_; }

private:
  [[nodiscard]] core::expected<void> open_next_file() noexcept;
  [[nodiscard]] std::string path_for(unsigned index) const;

  pal::IPlatform *platform_;
  std::string directory_;
  std::string base_name_;
  RotationPolicy policy_;

  std::unique_ptr<pal::IFile> file_;
  std::string current_path_;
  std::uint64_t bytes_in_file_ = 0;
  unsigned next_index_ = 0;
  unsigned rotations_ = 0;
  std::uint64_t discarded_records_ = 0;
  bool disk_is_full_ = false;
};

} // namespace volt::log
