#pragma once

#include <cstddef>
#include <cstdint>

namespace volt::memory {

/// Declares that the enclosing block does not allocate, and enforces it.
///
/// One line at the top of a function is what turns K10 from an intention into
/// a measurement (SPEC 0.2): every allocation made while the scope is alive is
/// counted, and in a debug build the first one stops the process where it
/// happened instead of letting a latency spike be discovered on a vehicle.
///
/// Scopes nest. A nested scope adds nothing, because the outer one already
/// forbids allocating; the guard is released when the outermost one ends.
///
/// Named as SPEC 8.3 names it. It is the one type in VOLT whose name is not
/// PascalCase, because the spelling in the specification is the spelling that
/// appears at every call site.
///
/// @thread the guard applies to the constructing thread only
/// @rt     allocation-free; costs one thread-local increment
class no_alloc_scope final {
public:
  /// Forbids allocation on this thread until the scope ends.
  no_alloc_scope() noexcept;

  /// Restores whatever the enclosing scope allowed.
  ~no_alloc_scope() noexcept;

  // Rule of five because the guard is the object's lifetime: a copy would
  // release the thread's guard early, and a move would leave two objects
  // both believing they own one release.
  no_alloc_scope(const no_alloc_scope &) = delete;
  no_alloc_scope &operator=(const no_alloc_scope &) = delete;
  no_alloc_scope(no_alloc_scope &&) = delete;
  no_alloc_scope &operator=(no_alloc_scope &&) = delete;

  /// Returns how many allocations happened since this scope began.
  ///
  /// Always zero in a debug build, where the first violation ends the process
  /// before anyone can ask.
  ///
  /// @thread the thread that constructed the scope
  /// @rt     allocation-free and O(1)
  [[nodiscard]] std::uint64_t violations() const noexcept;

  /// Reports whether the calling thread is currently inside a scope.
  ///
  /// @thread any
  /// @rt     allocation-free; one thread-local read
  [[nodiscard]] static bool active() noexcept;

  /// Handles an allocation of `bytes` that a scope had forbidden.
  ///
  /// Called by the replacement `operator new`, which is the only place that
  /// can see the violation happen. In a debug build it does not return.
  ///
  /// @thread the allocating thread
  /// @rt     counts and traces; in a debug build it ends the process
  static void report_violation(std::size_t bytes) noexcept;

private:
  std::uint64_t entry_violations_;
};

} // namespace volt::memory

namespace volt {

/// SPEC 8.3 spells this unqualified at every call site.
using no_alloc_scope = memory::no_alloc_scope;

} // namespace volt
