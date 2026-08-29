#include "sim_world.hpp"

#include <string>
#include <utility>

namespace volt::pal::sim::detail {

SimWorld::SimWorld(const SimConfig &config) noexcept
    : random_{config.seed}, network_{random_, config.network},
      now_ns_{config.monotonic_origin.ns_since_epoch()},
      realtime_offset_ns_{config.realtime_offset.ns()},
      file_system_capacity_bytes_{config.file_system_capacity_bytes} {}

void SimWorld::advance_to(std::int64_t target_ns) noexcept {
  if (target_ns <= now_ns_) {
    return;
  }
  now_ns_ = target_ns;
  record("clock.advance", static_cast<std::uint64_t>(now_ns_));
}

void SimWorld::advance_by(std::int64_t delta_ns) noexcept {
  if (delta_ns <= 0) {
    return;
  }
  advance_to(now_ns_ + delta_ns);
}

std::span<std::byte> SimWorld::create_region(std::string_view name, std::size_t bytes) {
  std::vector<std::byte> &region = regions_[std::string{name}];
  region.assign(bytes, std::byte{0});
  record("shm.create", static_cast<std::uint64_t>(bytes));
  return region;
}

std::optional<std::span<std::byte>> SimWorld::find_region(std::string_view name) {
  const auto entry = regions_.find(name);
  if (entry == regions_.end()) {
    return std::nullopt;
  }
  return std::span<std::byte>{entry->second};
}

std::vector<std::byte> &SimWorld::truncate_file(std::string_view path) {
  std::vector<std::byte> &content = files_[std::string{path}];
  content.clear();
  record("file.truncate", static_cast<std::uint64_t>(path.size()));
  return content;
}

std::vector<std::byte> &SimWorld::open_file(std::string_view path) {
  return files_[std::string{path}];
}

std::vector<std::byte> *SimWorld::find_file(std::string_view path) {
  const auto entry = files_.find(path);
  if (entry == files_.end()) {
    return nullptr;
  }
  return &entry->second;
}

void SimWorld::register_program(std::string_view path, ProcessExit outcome) {
  programs_.insert_or_assign(std::string{path}, outcome);
}

std::optional<ProcessExit> SimWorld::find_program(std::string_view path) const {
  const auto entry = programs_.find(path);
  if (entry == programs_.end()) {
    return std::nullopt;
  }
  return entry->second;
}

bool SimWorld::file_system_can_grow(std::size_t additional) const noexcept {
  if (file_system_capacity_bytes_ == 0) {
    return true;
  }
  std::uint64_t used = 0;
  for (const auto &[path, content] : files_) {
    used += content.size();
  }
  return used + additional <= file_system_capacity_bytes_;
}

std::int32_t SimWorld::next_process_id() noexcept {
  const std::int32_t identifier = next_process_id_;
  next_process_id_ += 1;
  return identifier;
}

} // namespace volt::pal::sim::detail
