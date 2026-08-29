#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <type_traits>

namespace volt::memory::detail {

/// Stores a trivially copyable value without requiring a default constructor.
template <typename T> class TrivialStorage final {
  static_assert(std::is_trivially_copyable_v<T>, "queue payloads must be trivially copyable");

public:
  constexpr TrivialStorage() noexcept = default;

  constexpr void store(const T &value) noexcept {
    bytes_ = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  }

  [[nodiscard]] constexpr T load() const noexcept { return std::bit_cast<T>(bytes_); }

private:
  std::array<std::byte, sizeof(T)> bytes_{};
};

} // namespace volt::memory::detail
