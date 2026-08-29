#pragma once

#include "file_descriptor.hpp"

#include "volt/pal/file.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace volt::pal::posix {

/// A file opened through the raw descriptor calls.
///
/// No buffering of its own: buffering belongs to the log ring, which already
/// decides what may be lost, and a second buffer here would decide it again
/// without knowing the policy.
class PosixFile final : public IFile {
public:
  /// Adopts an open descriptor. Only the platform calls this.
  PosixFile(detail::FileDescriptor descriptor, FileMode mode) noexcept
      : descriptor_{std::move(descriptor)}, mode_{mode} {}

  [[nodiscard]] core::expected<std::size_t> read(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<std::size_t>
  write(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<void> flush() noexcept override;
  [[nodiscard]] core::expected<std::uint64_t> size() const noexcept override;

private:
  detail::FileDescriptor descriptor_;
  FileMode mode_;
};

} // namespace volt::pal::posix
