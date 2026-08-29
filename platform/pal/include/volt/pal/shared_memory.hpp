#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace volt::pal {

/// A named region mapped into this process, used as the substrate for the
/// zero-copy transport in `platform/ipc`.
///
/// The object owns the mapping: it is unmapped when the object dies. Whoever
/// created the region also removes its name, so a crashed process does not
/// leave a stale region behind.
class ISharedMemory {
public:
  ISharedMemory() = default;
  virtual ~ISharedMemory() = default;

  // Deleted because the object owns a mapping; a copy would unmap twice.
  ISharedMemory(const ISharedMemory &) = delete;
  ISharedMemory &operator=(const ISharedMemory &) = delete;
  ISharedMemory(ISharedMemory &&) = delete;
  ISharedMemory &operator=(ISharedMemory &&) = delete;

  /// Returns the mapped bytes.
  ///
  /// @pre   the returned span is valid only while this object is alive
  /// @rt    allocation-free, no syscall
  [[nodiscard]] virtual std::span<std::byte> bytes() noexcept = 0;

  /// Returns the mapped bytes for reading.
  ///
  /// @pre the returned span is valid only while this object is alive
  [[nodiscard]] virtual std::span<const std::byte> bytes() const noexcept = 0;

  /// Returns the name the region was created or opened under.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace volt::pal
