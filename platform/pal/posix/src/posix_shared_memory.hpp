#pragma once

#include "volt/pal/shared_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace volt::pal::posix {

/// A POSIX shared memory object mapped into this process.
///
/// Only the creator unlinks the name. An opener that unlinked on close would
/// pull the region out from under the processes still using it.
class PosixSharedMemory final : public ISharedMemory {
public:
  /// Whether this object is responsible for removing the name.
  enum class Ownership : std::uint8_t { kCreator, kOpener };

  /// Adopts an existing mapping. Only the platform calls this.
  PosixSharedMemory(std::string name, std::span<std::byte> mapping, Ownership ownership) noexcept;

  ~PosixSharedMemory() override;

  [[nodiscard]] std::span<std::byte> bytes() noexcept override;
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

private:
  std::string name_;
  std::span<std::byte> mapping_;
  Ownership ownership_;
};

} // namespace volt::pal::posix
