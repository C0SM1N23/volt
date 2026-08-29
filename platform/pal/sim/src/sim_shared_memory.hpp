#pragma once

#include "volt/pal/shared_memory.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace volt::pal::sim {

/// A shared region, which in a single-process world is a buffer the world owns.
///
/// Two mappings of the same name are two views of the same bytes, exactly as
/// they would be across processes, so code written against the interface sees
/// the same sharing semantics either way.
class SimSharedMemory final : public ISharedMemory {
public:
  /// @pre `region` stays valid for as long as this object; the world owns it
  SimSharedMemory(std::string name, std::span<std::byte> region) noexcept
      : name_{std::move(name)}, region_{region} {}

  [[nodiscard]] std::span<std::byte> bytes() noexcept override { return region_; }

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept override { return region_; }

  [[nodiscard]] std::string_view name() const noexcept override { return name_; }

private:
  std::string name_;
  std::span<std::byte> region_;
};

} // namespace volt::pal::sim
