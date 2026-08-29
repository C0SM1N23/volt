#include "posix_shared_memory.hpp"

#include <sys/mman.h>
#include <utility>

namespace volt::pal::posix {

PosixSharedMemory::PosixSharedMemory(std::string name, std::span<std::byte> mapping,
                                     Ownership ownership) noexcept
    : name_{std::move(name)}, mapping_{mapping}, ownership_{ownership} {}

PosixSharedMemory::~PosixSharedMemory() {
  if (!mapping_.empty()) {
    static_cast<void>(::munmap(mapping_.data(), mapping_.size()));
  }
  if (ownership_ == Ownership::kCreator) {
    static_cast<void>(::shm_unlink(name_.c_str()));
  }
}

std::span<std::byte> PosixSharedMemory::bytes() noexcept { return mapping_; }

std::span<const std::byte> PosixSharedMemory::bytes() const noexcept { return mapping_; }

std::string_view PosixSharedMemory::name() const noexcept { return name_; }

} // namespace volt::pal::posix
