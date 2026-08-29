#pragma once

#include "allocation_stats.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace volt::memory {

/// Most threads whose allocations are counted separately.
///
/// The registry a reader walks is a fixed array rather than a growing one, so
/// a thread claims its slot with one atomic step and reading the process total
/// stays bounded (SPEC 5.5). It is the limit the logger and the tracer already
/// place on the threads of a VOLT process.
inline constexpr std::size_t kMaxTrackedThreads = 64;

/// Counts what every thread allocates, so K10 can be stated as a number.
///
/// A singleton, which AGENTS.md 2.9 permits for exactly this shape of thing:
/// the replacement `operator new` has to reach it with nothing to thread an
/// argument through, and a process has one heap to account for.
///
/// The counters live here rather than in thread-local storage because that
/// storage goes away when a thread exits, while what the thread allocated
/// still has to be readable afterwards. A slot is claimed once per thread and
/// never released, which is what keeps recording wait-free.
class AllocationTracker final {
public:
  /// Returns the process-wide tracker.
  [[nodiscard]] static AllocationTracker &instance() noexcept;

  /// Credits one allocation of `bytes` to the calling thread.
  ///
  /// @post   returns the slot the block must be credited back to, which is
  ///         kMaxTrackedThreads when this thread found the registry full
  /// @thread any
  /// @rt     allocation-free and wait-free
  [[nodiscard]] static std::size_t record_allocation(std::size_t bytes) noexcept;

  /// Returns `bytes` to the slot that `record_allocation` handed out.
  ///
  /// The owning slot is named explicitly because a block is very often freed
  /// by a thread other than the one that allocated it; crediting the freeing
  /// thread instead would drive its live total below zero.
  ///
  /// @pre    `bytes` and `owner_index` are what `record_allocation` reported
  /// @thread any
  /// @rt     allocation-free and wait-free
  static void record_deallocation(std::size_t bytes, std::size_t owner_index) noexcept;

  /// Adds one guard violation to the calling thread's counters.
  ///
  /// @thread any
  /// @rt     allocation-free and wait-free
  static void record_violation() noexcept;

  /// Returns the calling thread's counters.
  ///
  /// @thread any
  /// @rt     allocation-free and O(1)
  [[nodiscard]] static AllocationStats current_thread_stats() noexcept;

  /// Returns the sum of every tracked thread's counters.
  ///
  /// Threads are summed one after another while they keep running, so the
  /// result is a true total for no single instant. It answers "how much has
  /// this process allocated", not "what does the heap hold exactly now".
  ///
  /// @thread any
  /// @rt     walks up to kMaxTrackedThreads slots; not for the control loop
  [[nodiscard]] AllocationStats process_stats() const noexcept;

  /// Reports whether VOLT's replacement allocator is the one in use.
  ///
  /// It is not always: a sanitizer runtime defines the allocation operators
  /// itself, and the linker takes those instead, which leaves every counter
  /// here at zero. Anything that would otherwise publish a reassuring zero has
  /// to ask this first, because "nothing allocated" and "nothing was counted"
  /// look identical in the numbers.
  ///
  /// Answered by allocating once and watching the counters rather than by
  /// asking the build, because whether the operators were linked in depends on
  /// what else in the program defines them.
  ///
  /// @thread any
  /// @rt     allocates on the first call; free of cost afterwards
  [[nodiscard]] static bool hooks_installed() noexcept;

  /// Returns how many threads have claimed a slot.
  [[nodiscard]] std::size_t tracked_threads() const noexcept;

  /// Returns how many threads found the registry full and stayed uncounted.
  [[nodiscard]] std::uint64_t threads_refused() const noexcept;

private:
  /// One thread's counters.
  ///
  /// Atomic because a monitoring thread reads them while the owner keeps
  /// counting, and because a block allocated here may be released from
  /// another thread. Every access is relaxed: the counters order nothing but
  /// themselves, and a total that arrives a few allocations late is still the
  /// answer to the question being asked.
  class ThreadCounters final {
  public:
    std::atomic<std::uint64_t> allocation_count{0};
    std::atomic<std::uint64_t> deallocation_count{0};
    std::atomic<std::uint64_t> total_bytes{0};
    std::atomic<std::uint64_t> live_bytes{0};
    std::atomic<std::uint64_t> live_allocations{0};
    std::atomic<std::uint64_t> peak_live_bytes{0};
    std::atomic<std::uint64_t> peak_live_allocations{0};
    std::atomic<std::uint64_t> violation_count{0};
  };

  // Marks a thread that has not looked for its slot yet. Distinct from
  // kMaxTrackedThreads, which marks a thread that looked and was refused, so
  // a refused thread does not retry the registry on every allocation.
  static constexpr std::size_t kUnclaimedSlot = kMaxTrackedThreads + 1;

  AllocationTracker() = default;

  /// Returns the calling thread's slot, claiming one on first use.
  ///
  /// @post returns kMaxTrackedThreads once that many threads have claimed one
  [[nodiscard]] static std::size_t slot_of_current_thread() noexcept;

  /// Returns a copy of the counters in `index`.
  [[nodiscard]] AllocationStats stats_of_slot(std::size_t index) const noexcept;

  std::array<ThreadCounters, kMaxTrackedThreads> threads_{};

  // Hands each new thread a slot of its own, and keeps counting past the end
  // so that a refusal is visible rather than silently overwriting slot zero.
  std::atomic<std::size_t> claimed_slots_{0};

  std::atomic<std::uint64_t> threads_refused_{0};
};

} // namespace volt::memory
