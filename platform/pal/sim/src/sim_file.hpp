#pragma once

#include "sim_world.hpp"

#include "volt/pal/file.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace volt::pal::sim {

/// A file held in memory.
///
/// Content survives the object, exactly as a file survives a descriptor: it
/// belongs to the world, which is what lets a test write, close and reopen.
class SimFile final : public IFile {
public:
  /// @pre `world` and `content` outlive this file; the world owns the content
  SimFile(detail::SimWorld &world, std::vector<std::byte> &content, FileMode mode) noexcept
      : world_{&world}, content_{&content}, mode_{mode} {}

  [[nodiscard]] core::expected<std::size_t> read(std::span<std::byte> buffer) noexcept override;
  [[nodiscard]] core::expected<std::size_t>
  write(std::span<const std::byte> payload) noexcept override;
  [[nodiscard]] core::expected<void> flush() noexcept override;
  [[nodiscard]] core::expected<std::uint64_t> size() const noexcept override;

private:
  detail::SimWorld *world_;
  std::vector<std::byte> *content_;
  FileMode mode_;
  std::size_t offset_ = 0;
};

} // namespace volt::pal::sim
