#pragma once

#include "volt/core/error.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace volt::memory {

/// Publishes the latest trivially copyable value to multiple non-blocking readers.
///
/// Atomic words keep the C++ memory model and TSan free of data races while the
/// sequence detects a mixed snapshot. One read attempt is deliberately bounded;
/// collision with the sole writer is reported as `kResourceBusy`.
///
/// @thread one writer calls `store`; any number of readers call `load`
/// @rt     allocation-free and non-blocking
template <typename T> class SeqLock final {
  static_assert(std::is_trivially_copyable_v<T>, "seqlock values must be trivially copyable");
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "VOLT targets require lock-free 64-bit atomics");

public:
  /// Constructs a lock containing the value-initialized representation of `T`.
  SeqLock() noexcept = default;

  // Rule of five because readers synchronize through this sequence address,
  // moving it would split one publication history across two objects.
  ~SeqLock() noexcept = default;
  SeqLock(const SeqLock &) = delete;
  SeqLock &operator=(const SeqLock &) = delete;
  SeqLock(SeqLock &&) = delete;
  SeqLock &operator=(SeqLock &&) = delete;

  /// Publishes a new latest value.
  /// @thread the single writer
  /// @rt     allocation-free and bounded by the number of native words in `T`
  void store(const T &value) noexcept {
    // Acquire prevents payload stores from moving before the odd marker, and
    // release keeps earlier publications ordered before this writer epoch.
    const std::uint64_t odd = sequence_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    store_payload(value);
    // Release publishes every byte before the even sequence observed by a
    // reader's acquire load.
    sequence_.store(odd + 1U, std::memory_order_release);
  }

  /// Reads one internally consistent snapshot.
  /// @thread any reader
  /// @rt     allocation-free and bounded by the number of native words in `T`
  /// @errors kResourceBusy when the writer overlaps this attempt
  [[nodiscard]] core::expected<T> load() const noexcept {
    // Acquire observes the byte stores released by the most recent completed
    // writer before the reader starts copying them.
    const std::uint64_t before = sequence_.load(std::memory_order_acquire);
    if ((before & kWriterActiveBit) != 0U) {
      count_read_failure();
      return std::unexpected{core::ErrorCode::kResourceBusy};
    }

    const std::array<std::byte, sizeof(T)> bytes = load_payload();
    // A zero RMW reads the immediately preceding modification in the atomic
    // order, so it cannot validate against a stale pre-writer value. Its
    // release half keeps every byte load before validation, and its acquire
    // half makes the operation fully visible to TSan.
    const std::uint64_t after = sequence_.fetch_add(0U, std::memory_order_acq_rel);
    if (before != after) {
      count_read_failure();
      return std::unexpected{core::ErrorCode::kResourceBusy};
    }
    return std::bit_cast<T>(bytes);
  }

  /// Returns how many reads overlapped a publication.
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t read_failures() const noexcept {
    return read_failures_.load(std::memory_order_relaxed);
  }

private:
  // Bit zero is the writer-active flag because every completed publication
  // advances the sequence by two. Changing it breaks the odd/even protocol.
  static constexpr std::uint64_t kWriterActiveBit = 1U;
  // Eight bytes are the native lock-free atomic width on the x86-64 and ARMv8
  // targets in SPEC 3.1. A narrower unit multiplies reader cost; a wider one
  // is not lock-free on every target.
  static constexpr std::size_t kAtomicWordBytes = sizeof(std::uint64_t);
  static constexpr std::size_t kAtomicWordCount =
      (sizeof(T) + kAtomicWordBytes - 1U) / kAtomicWordBytes;

  void store_payload(const T &value) noexcept {
    const std::array<std::byte, sizeof(T)> bytes =
        std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    VOLT_LOOP_BOUND(kAtomicWordCount);
    for (std::size_t index = 0; index < kAtomicWordCount; ++index) {
      const std::size_t offset_bytes = index * kAtomicWordBytes;
      const std::size_t remaining_bytes = sizeof(T) - offset_bytes;
      const std::size_t copied_bytes =
          remaining_bytes < kAtomicWordBytes ? remaining_bytes : kAtomicWordBytes;
      std::uint64_t word = 0;
      static_cast<void>(std::memcpy(&word, bytes.data() + offset_bytes, copied_bytes));
      payload_[index].store(word, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] std::array<std::byte, sizeof(T)> load_payload() const noexcept {
    std::array<std::byte, sizeof(T)> bytes{};
    VOLT_LOOP_BOUND(kAtomicWordCount);
    for (std::size_t index = 0; index < kAtomicWordCount; ++index) {
      // Atomic words reduce coherency operations eightfold versus atomic bytes,
      // sequence validation still rejects a snapshot spanning publications.
      const std::uint64_t word = payload_[index].load(std::memory_order_relaxed);
      const std::size_t offset_bytes = index * kAtomicWordBytes;
      const std::size_t remaining_bytes = sizeof(T) - offset_bytes;
      const std::size_t copied_bytes =
          remaining_bytes < kAtomicWordBytes ? remaining_bytes : kAtomicWordBytes;
      static_cast<void>(std::memcpy(bytes.data() + offset_bytes, &word, copied_bytes));
    }
    return bytes;
  }

  void count_read_failure() const noexcept {
    read_failures_.fetch_add(1U, std::memory_order_relaxed);
  }

  // The writer alone advances the sequence; readers only observe it.
  mutable std::atomic<std::uint64_t> sequence_{0};
  // The writer stores each native word; all readers only load. Atomic words
  // avoid a language-level data race even for a rejected mixed snapshot.
  std::array<std::atomic<std::uint64_t>, kAtomicWordCount> payload_{};
  // Readers jointly update this diagnostic; it orders no publication data.
  mutable std::atomic<std::uint64_t> read_failures_{0};
};

} // namespace volt::memory
