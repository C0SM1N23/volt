#include "volt/memory/no_alloc_scope.hpp"

#include "volt/memory/allocation_tracker.hpp"

#include "volt/core/error.hpp"
#include "volt/trace/trace_event.hpp"
#include "volt/trace/tracer.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(NDEBUG)
#include <cstdio>
#include <stacktrace>
#include <string>
#endif

namespace volt::memory {
namespace {

/// How many scopes the calling thread has open.
///
/// initial-exec for the reason the tracer gives for its own thread-local: the
/// default model calls into the dynamic loader, and this one is read on every
/// allocation the process makes.
[[nodiscard]] std::uint32_t &guard_depth() noexcept {
  [[gnu::tls_model("initial-exec")]] static thread_local std::uint32_t depth = 0;
  return depth;
}

/// Whether this thread is in the middle of reporting a violation.
///
/// Reporting allocates — building a stack trace and its text both do — and
/// those allocations must not be reported in their turn. Without this the
/// first violation would recurse until the stack ran out.
[[nodiscard]] bool &reporting_violation() noexcept {
  [[gnu::tls_model("initial-exec")]] static thread_local bool reporting = false;
  return reporting;
}

#if !defined(NDEBUG)

/// Writes `text` to the standard error stream.
///
/// AGENTS.md 2.5 sends output to `platform/log`, which this one call cannot
/// use: the log is a binary ring drained by another thread, and this function
/// is followed by an abort that the ring will not survive. A violation that
/// killed the process has to say where it happened while the process is still
/// alive to say it. Nothing else in `platform/` writes to a stream.
void write_to_standard_error(std::string_view text) noexcept {
  static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
}

/// Prints where the forbidden allocation came from, then ends the process.
[[noreturn]] void abort_with_backtrace(std::size_t bytes) noexcept {
  write_to_standard_error("volt: allocation of ");
  write_to_standard_error(std::to_string(bytes));
  write_to_standard_error(" bytes inside a no_alloc_scope\n");
  write_to_standard_error(std::to_string(std::stacktrace::current()));
  write_to_standard_error("\n");
  core::assert_failed("!no_alloc_scope::active()", "allocation inside a no_alloc_scope", __FILE__,
                      __LINE__);
}

#endif

} // namespace

no_alloc_scope::no_alloc_scope() noexcept
    : entry_violations_{AllocationTracker::current_thread_stats().violation_count} {
  ++guard_depth();
}

no_alloc_scope::~no_alloc_scope() noexcept { --guard_depth(); }

std::uint64_t no_alloc_scope::violations() const noexcept {
  return AllocationTracker::current_thread_stats().violation_count - entry_violations_;
}

bool no_alloc_scope::active() noexcept { return guard_depth() > 0 && !reporting_violation(); }

void no_alloc_scope::report_violation(std::size_t bytes) noexcept {
  reporting_violation() = true;
  AllocationTracker::record_violation();

  // The size is what tells a violation apart in a capture, and the record
  // carries a 32-bit argument. A block larger than four gigabytes is reported
  // at the ceiling rather than wrapping to a small and misleading number.
  VOLT_TRACE(trace::TraceEvent::kAllocationViolation,
             static_cast<std::uint32_t>(
                 std::min<std::size_t>(bytes, std::numeric_limits<std::uint32_t>::max())));

#if !defined(NDEBUG)
  abort_with_backtrace(bytes);
#else
  // A release build keeps running: the counter and the trace record are what
  // SPEC 8.3 asks for, and P53 is where a fault manager will read them.
  reporting_violation() = false;
#endif
}

} // namespace volt::memory
