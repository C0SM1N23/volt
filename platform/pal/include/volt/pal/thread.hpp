#pragma once

#include "volt/core/duration.hpp"
#include "volt/core/error.hpp"
#include "volt/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace volt::pal {

/// Scheduling policy a thread runs under.
enum class SchedulingPolicy : std::uint8_t {
  /// The default time-sharing policy. Everything on the control plane.
  kOther,
  /// Real-time, run until it blocks or a higher priority preempts it.
  kFifo,
  /// Real-time with a time slice among equal priorities.
  kRoundRobin,
  /// Earliest-deadline-first: the thread carries a reservation rather than a
  /// priority, so `DeadlineParameters` replace `priority` for it.
  kDeadline,
};

/// Bit per CPU. Zero means the thread inherits whatever affinity it was given,
/// which is what a non-isolated thread wants.
///
/// A 64-bit mask covers every deployment target in SPEC 3.1; a machine with
/// more cores would need a wider mask here and in the POSIX backend.
using CpuMask = std::uint64_t;

/// Longest thread name a backend is required to keep.
///
/// Chosen from the tightest target: Linux accepts 16 bytes including the
/// terminator. Stating it here rather than in one backend means every backend
/// truncates at the same point, so a name is not a portability surprise.
inline constexpr std::size_t kMaxThreadNameLength = 15;

/// What a thread must be told at creation. SPEC 42.2 requires every VOLT
/// thread to state its name, priority and affinity rather than inherit them.
/// Reservation a deadline-scheduled thread asks the kernel for.
///
/// The triple is the sporadic task model the EDF class of SPEC 9.2 runs on:
/// the thread is guaranteed `runtime` of CPU in every `period`, and the
/// kernel expects it to be done `deadline` after each release. Unlike a
/// fixed priority, this is a contract the kernel admits or refuses, and it
/// enforces the runtime side by throttling a thread that overruns.
struct DeadlineParameters {
  /// CPU budget per period. Must be positive and at most `deadline`.
  core::Duration runtime;
  /// Relative deadline. Must be positive and at most `period`.
  core::Duration deadline;
  /// Release interval. Must be positive.
  core::Duration period;
};

struct ThreadConfig {
  /// Shown by the kernel and every profiler. Truncated by the platform if it
  /// exceeds what the OS accepts.
  std::string_view name;
  SchedulingPolicy policy = SchedulingPolicy::kOther;
  /// Meaningful only for the real-time policies.
  core::Priority priority{};
  CpuMask cpu_mask = 0;
  /// Zero asks the platform for its default stack.
  std::size_t stack_bytes = 0;
};

/// Body of a thread. Move-only so it can own what it captures.
using ThreadEntry = std::move_only_function<void()>;

/// A running thread of execution.
///
/// The object owns the thread: destroying it without joining is a programming
/// error the backend reports the way it reports any other invariant violation.
class IThread {
public:
  IThread() = default;
  virtual ~IThread() = default;

  // Deleted so a thread handle cannot be sliced or silently duplicated: two
  // handles onto one thread would make ownership of the join ambiguous.
  IThread(const IThread &) = delete;
  IThread &operator=(const IThread &) = delete;
  IThread(IThread &&) = delete;
  IThread &operator=(IThread &&) = delete;

  /// Waits for the thread body to finish.
  ///
  /// @pre    the thread has not been joined yet
  /// @post   the body has run to completion and the thread is not joinable
  /// @thread any thread other than this one
  /// @rt     blocks; init and shutdown only
  /// @errors kInternalOutOfRange when the thread was already joined
  [[nodiscard]] virtual core::expected<void> join() noexcept = 0;

  /// Reports whether the thread still has to be joined.
  [[nodiscard]] virtual bool joinable() const noexcept = 0;

  /// Returns the name the thread was created with.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace volt::pal
