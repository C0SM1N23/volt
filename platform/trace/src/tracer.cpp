#include "volt/trace/tracer.hpp"

#include <string>

namespace volt::trace {
namespace {

/// Records the calling thread's place in the registry.
///
/// A plain index rather than a pointer, because the record carries the index
/// and the collector needs it to name the track an event belongs to.
struct ThreadSlot {
  std::size_t index = kMaxTracedThreads;
  TraceRing *ring = nullptr;
};

/// The calling thread's slot.
///
/// initial-exec rather than the default TLS model: with the default, reading a
/// thread-local from a library goes through a call into the dynamic loader,
/// which on a trace point costs more than the event being recorded. The model
/// is available because the tracer is linked into the program rather than
/// loaded at run time.
[[nodiscard]] ThreadSlot &slot_of_current_thread() noexcept {
  [[gnu::tls_model("initial-exec")]] static thread_local ThreadSlot slot;
  return slot;
}

} // namespace

Tracer &Tracer::instance() noexcept {
  // Function-local static: built on first use, destroyed after main, which is
  // later than any thread that might still be tracing.
  static Tracer tracer;
  return tracer;
}

void Tracer::set_state(TraceState state) noexcept {
  state_.store(state == TraceState::kEnabled ? 1 : 0, std::memory_order_relaxed);
}

void Tracer::calibrate(pal::IClock &clock, core::Duration window) noexcept {
  cycle_clock_ = CycleClock::calibrate(clock, window);
}

std::size_t Tracer::create_ring(std::string_view name) noexcept {
  // fetch_add rather than a load and a store: several threads may register at
  // once, and each has to get an index of its own.
  const std::size_t index = next_ring_.fetch_add(1, std::memory_order_relaxed);
  if (index >= kMaxTracedThreads) {
    return kMaxTracedThreads;
  }

  owned_rings_[index] = std::make_unique<TraceRing>();
  rings_[index] = owned_rings_[index].get();
  thread_names_[index] = std::string{name};

  // Release: the ring and its name must be visible before the count that
  // exposes them. The count only grows over contiguous slots, so a thread that
  // claimed a later index waits for the earlier ones to publish.
  std::size_t expected = index;
  while (!ring_count_.compare_exchange_weak(expected, index + 1, std::memory_order_release,
                                            std::memory_order_relaxed)) {
    expected = index;
  }
  return index;
}

bool Tracer::prepare_current_thread(std::string_view name) noexcept {
  ThreadSlot &slot = slot_of_current_thread();
  if (slot.ring != nullptr) {
    return true;
  }
  slot.index = instance().create_ring(name);
  if (slot.index >= kMaxTracedThreads) {
    return false;
  }
  slot.ring = instance().rings()[slot.index];
  return true;
}

std::size_t Tracer::current_thread_index() noexcept { return slot_of_current_thread().index; }

std::span<TraceRing *const> Tracer::rings() noexcept {
  // Acquire: pairs with the release in create_ring, so every ring counted here
  // is fully built.
  const std::size_t count = ring_count_.load(std::memory_order_acquire);
  return std::span<TraceRing *const>{rings_.data(), count};
}

std::string_view Tracer::thread_name(std::size_t index) const noexcept {
  if (index >= kMaxTracedThreads) {
    return {};
  }
  return thread_names_[index];
}

std::uint64_t Tracer::dropped_records() const noexcept {
  std::uint64_t total = 0;
  const std::size_t count = ring_count_.load(std::memory_order_acquire);
  for (std::size_t index = 0; index < count; ++index) {
    if (rings_[index] != nullptr) {
      total += rings_[index]->dropped();
    }
  }
  return total;
}

void emit(TraceEvent event, std::uint32_t argument) noexcept {
  const ThreadSlot &slot = slot_of_current_thread();
  if (slot.ring == nullptr) {
    // A thread that never registered is not traced. Registering here instead
    // would allocate inside the control loop this call exists to measure.
    return;
  }
  slot.ring->push(TraceRecord{.cycles = read_cycles(),
                              .event = event,
                              .node_id = 0,
                              .thread_index = static_cast<std::uint8_t>(slot.index),
                              .argument = argument});
}

TraceScope::TraceScope(TraceEvent begin_event, TraceEvent end_event,
                       std::uint32_t argument) noexcept
    : end_event_{end_event}, argument_{argument},
      // Whether the interval is recorded is decided once, at the start. A
      // scope that opened while tracing was on must close even if it is turned
      // off in between, or the timeline is left with an unmatched begin.
      recording_{Tracer::instance().enabled()} {
  if (recording_) {
    emit(begin_event, argument_);
  }
}

TraceScope::~TraceScope() {
  if (recording_) {
    emit(end_event_, argument_);
  }
}

} // namespace volt::trace
