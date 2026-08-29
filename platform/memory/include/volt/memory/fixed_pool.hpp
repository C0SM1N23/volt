#pragma once

#include "pool_index.hpp"

#include "volt/core/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace volt::memory {

/// Owns a fixed set of slots through a non-atomic, index-based free list.
///
/// @thread one owning thread
/// @rt     allocation-free and O(1)
template <typename T, std::size_t N> class FixedPool final {
  static_assert(N > 0, "a fixed pool needs at least one slot");
  static_assert(N < std::numeric_limits<std::uint32_t>::max(), "pool indices must fit uint32");
  static_assert(std::is_trivially_copyable_v<T>, "shared-memory pool values must be trivial");
  static_assert(std::is_default_constructible_v<T>, "pool slots must be default constructible");

public:
  /// Initializes every slot as available.
  FixedPool() noexcept {
    for (std::size_t index = 0; index < N; ++index) {
      next_[index] = static_cast<std::uint32_t>(index + 1U);
    }
  }

  // Rule of five because an outstanding PoolIndex designates this exact
  // object; copying or moving it would make ownership of that index ambiguous.
  ~FixedPool() noexcept = default;
  FixedPool(const FixedPool &) = delete;
  FixedPool &operator=(const FixedPool &) = delete;
  FixedPool(FixedPool &&) = delete;
  FixedPool &operator=(FixedPool &&) = delete;

  /// Claims one slot by its relocatable index.
  /// @post   the returned index remains owned until `release` succeeds
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kResourceExhausted when every slot is owned
  [[nodiscard]] core::expected<PoolIndex> allocate() noexcept {
    if (head_ == sentinel()) {
      ++allocation_failures_;
      return std::unexpected{core::ErrorCode::kResourceExhausted};
    }

    const std::uint32_t index = head_;
    head_ = next_[index];
    allocated_[index] = true;
    --available_;
    return PoolIndex{index};
  }

  /// Returns a slot to the free list.
  /// @pre    `index` was returned by this pool and has not already been released
  /// @post   the slot may be claimed by a later `allocate`
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalOutOfRange when the index is foreign or already free
  [[nodiscard]] core::expected<void> release(PoolIndex index) noexcept {
    const std::uint32_t value = index.value();
    if (value >= N || !allocated_[value]) {
      ++release_failures_;
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }

    allocated_[value] = false;
    next_[value] = head_;
    head_ = value;
    ++available_;
    return {};
  }

  /// Returns the mutable value in an owned slot.
  /// @pre    the returned reference is not retained after `release(index)`
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalOutOfRange when the index is foreign or not owned
  [[nodiscard]] core::expected<std::reference_wrapper<T>> get(PoolIndex index) noexcept {
    if (!owns(index)) {
      ++access_failures_;
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }
    return std::ref(values_[index.value()]);
  }

  /// Returns the immutable value in an owned slot.
  /// @pre    the returned reference is not retained after `release(index)`
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  /// @errors kInternalOutOfRange when the index is foreign or not owned
  [[nodiscard]] core::expected<std::reference_wrapper<const T>>
  get(PoolIndex index) const noexcept {
    if (!owns(index)) {
      ++access_failures_;
      return std::unexpected{core::ErrorCode::kInternalOutOfRange};
    }
    return std::cref(values_[index.value()]);
  }

  /// Returns the compile-time slot capacity.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// Returns how many slots can be claimed immediately.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::size_t available() const noexcept { return available_; }

  /// Returns how many allocations found the pool exhausted.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t allocation_failures() const noexcept { return allocation_failures_; }

  /// Returns how many invalid releases were rejected.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t release_failures() const noexcept { return release_failures_; }

  /// Returns how many accesses rejected a foreign or free index.
  /// @thread the owning thread
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t access_failures() const noexcept { return access_failures_; }

private:
  [[nodiscard]] static constexpr std::uint32_t sentinel() noexcept {
    return static_cast<std::uint32_t>(N);
  }

  [[nodiscard]] bool owns(PoolIndex index) const noexcept {
    return index.value() < N && allocated_[index.value()];
  }

  std::array<T, N> values_{};
  std::array<std::uint32_t, N> next_{};
  std::array<bool, N> allocated_{};
  std::uint32_t head_ = 0;
  std::size_t available_ = N;
  std::uint64_t allocation_failures_ = 0;
  std::uint64_t release_failures_ = 0;
  mutable std::uint64_t access_failures_ = 0;
};

} // namespace volt::memory
