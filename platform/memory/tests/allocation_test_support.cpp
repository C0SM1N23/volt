#include "allocation_test_support.hpp"

namespace volt::memory::test_support {

std::vector<std::byte> allocate_block(std::size_t size_bytes) {
  return std::vector<std::byte>(size_bytes);
}

} // namespace volt::memory::test_support
