#pragma once

#include "volt/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace volt::pal {

/// How a file is opened.
enum class FileMode : std::uint8_t {
  /// Existing file, reading only.
  kRead,
  /// Created or truncated, writing only.
  kWrite,
  /// Created if absent, writes go to the end.
  kAppend,
};

/// An open file.
///
/// Every write here is blocking, so nothing on the control path may call it
/// directly: log and trace writes reach the disk through `io-offload`
/// (SPEC 42.2).
class IFile {
public:
  IFile() = default;
  virtual ~IFile() = default;

  // Deleted because the object owns a file descriptor.
  IFile(const IFile &) = delete;
  IFile &operator=(const IFile &) = delete;
  IFile(IFile &&) = delete;
  IFile &operator=(IFile &&) = delete;

  /// Reads at most `buffer.size()` bytes, returning how many were read. A
  /// zero-length result means end of file.
  ///
  /// @pre    the file was opened for reading; `buffer` outlives the call
  /// @rt     blocks; not for the control loop
  /// @errors kResourceUnavailable when the file was not opened for reading
  [[nodiscard]] virtual core::expected<std::size_t> read(std::span<std::byte> buffer) noexcept = 0;

  /// Writes the whole payload, returning how many bytes were accepted.
  ///
  /// @pre    the file was opened for writing; `payload` outlives the call
  /// @rt     blocks; not for the control loop
  /// @errors kResourceExhausted when the filesystem is full,
  ///         kResourceUnavailable when the file was not opened for writing
  [[nodiscard]] virtual core::expected<std::size_t>
  write(std::span<const std::byte> payload) noexcept = 0;

  /// Pushes buffered writes to the operating system.
  ///
  /// @post   what was written before the call is visible to other readers
  [[nodiscard]] virtual core::expected<void> flush() noexcept = 0;

  /// Returns the current size in bytes.
  [[nodiscard]] virtual core::expected<std::uint64_t> size() const noexcept = 0;
};

} // namespace volt::pal
