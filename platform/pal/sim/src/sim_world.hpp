#pragma once

#include "sim_event_log.hpp"
#include "sim_network.hpp"
#include "sim_random.hpp"
#include "sim_scheduler.hpp"

#include "volt/pal/process.hpp"
#include "volt/pal/sim/sim_config.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace volt::pal::sim::detail {

/// Everything a simulated world contains, and the only mutable state behind
/// the objects the simulation backend hands out.
///
/// One owner, one thread, no synchronisation: the world exists precisely so
/// that a whole cluster can run inside a single thread (SPEC 21.1), and adding
/// a lock here would admit an interleaving the design rules out.
///
/// The objects created from this world hold a reference to it, so the world
/// must outlive every one of them.
class SimWorld final {
public:
  explicit SimWorld(const SimConfig &config) noexcept;

  // Rule of five because members hold references into each other; moving the
  // world would leave the network pointing at a generator that no longer
  // belongs to it.
  ~SimWorld() = default;
  SimWorld(const SimWorld &) = delete;
  SimWorld &operator=(const SimWorld &) = delete;
  SimWorld(SimWorld &&) = delete;
  SimWorld &operator=(SimWorld &&) = delete;

  /// Returns the virtual monotonic time.
  [[nodiscard]] std::int64_t now_ns() const noexcept { return now_ns_; }

  /// Returns the virtual wall-clock time.
  [[nodiscard]] std::int64_t realtime_ns() const noexcept { return now_ns_ + realtime_offset_ns_; }

  /// Moves time forward to `target_ns`, doing nothing if it is already past.
  ///
  /// @post the clock never moves backwards, which is what callers measuring an
  ///       interval rely on
  void advance_to(std::int64_t target_ns) noexcept;

  /// Moves time forward by `delta_ns`.
  ///
  /// @pre `delta_ns` is not negative
  void advance_by(std::int64_t delta_ns) noexcept;

  [[nodiscard]] SimRandom &random() noexcept { return random_; }
  [[nodiscard]] SimScheduler &scheduler() noexcept { return scheduler_; }
  [[nodiscard]] SimNetwork &network() noexcept { return network_; }

  /// Folds an event into the run's digest.
  void record(std::string_view name, std::uint64_t value) noexcept { events_.record(name, value); }

  /// Returns the digest of everything that happened so far.
  [[nodiscard]] std::uint64_t event_digest() const noexcept { return events_.digest(); }

  /// Alignment every simulated region starts on.
  ///
  /// A real mapping arrives from the kernel page-aligned, and whatever a
  /// caller lays out inside it may rely on that - an over-aligned type in a
  /// shared segment is normal, cache-line padding being the usual reason.
  /// A plain vector only promises `max_align_t`, so a region backed by one
  /// would let the simulation accept layouts the real backend rejects.
  static constexpr std::size_t kRegionAlignment = 4096;

  /// Creates a zero-filled region, replacing any region of that name.
  ///
  /// @post the returned span starts on kRegionAlignment and stays valid
  ///       until the region is created again
  [[nodiscard]] std::span<std::byte> create_region(std::string_view name, std::size_t bytes);

  /// Returns an existing region, or nothing when the name is unknown.
  [[nodiscard]] std::optional<std::span<std::byte>> find_region(std::string_view name);

  /// Empties a file, creating it when absent.
  ///
  /// @post the reference stays valid for the life of the world
  [[nodiscard]] std::vector<std::byte> &truncate_file(std::string_view path);

  /// Returns a file, creating it when absent.
  [[nodiscard]] std::vector<std::byte> &open_file(std::string_view path);

  /// Returns a file, or nothing when it does not exist.
  [[nodiscard]] std::vector<std::byte> *find_file(std::string_view path);

  /// Reports whether the filesystem can take `additional` more bytes.
  [[nodiscard]] bool file_system_can_grow(std::size_t additional) const noexcept;

  /// Declares that a program exists at `path` and how it ends.
  void register_program(std::string_view path, ProcessExit outcome);

  /// Returns how the program at `path` ends, or nothing when none is there.
  [[nodiscard]] std::optional<ProcessExit> find_program(std::string_view path) const;

  /// Declares where the simulated watchdog device answers.
  void set_watchdog_path(std::string_view path) { watchdog_path_ = path; }

  /// Reports whether `path` is the watchdog device.
  [[nodiscard]] bool is_watchdog_path(std::string_view path) const {
    return !watchdog_path_.empty() && path == watchdog_path_;
  }

  /// Returns the next simulated process identifier.
  [[nodiscard]] std::int32_t next_process_id() noexcept;

  /// The identifier of the process the simulation itself runs as.
  ///
  /// Spawned processes start above it, so the self identifier can never
  /// collide with a child's.
  static constexpr std::int32_t kSelfProcessId = 1;

  /// Marks `identifier` as existing, the moment a process is spawned.
  void register_process(std::int32_t identifier) { processes_alive_[identifier] = true; }

  /// Marks `identifier` as reaped, after which it no longer exists.
  void mark_process_reaped(std::int32_t identifier) { processes_alive_[identifier] = false; }

  /// Reports whether process `identifier` exists, matching the kernel's view: an
  /// exited child keeps existing until someone waits for it.
  [[nodiscard]] bool process_alive(std::int32_t identifier) const {
    if (identifier == kSelfProcessId) {
      return true;
    }
    const auto entry = processes_alive_.find(identifier);
    return entry != processes_alive_.end() && entry->second;
  }

  /// One simulated kernel message queue.
  struct MessageQueueState {
    std::uint32_t depth = 0;
    std::uint32_t message_bytes = 0;
    std::deque<std::vector<std::byte>> messages;
  };

  /// Creates a queue, replacing any queue of that name.
  [[nodiscard]] MessageQueueState &create_message_queue(std::string_view name, std::uint32_t depth,
                                                        std::uint32_t message_bytes);

  /// Returns a queue, or nothing when the name is unknown.
  [[nodiscard]] MessageQueueState *find_message_queue(std::string_view name);

  /// Removes a queue, when its creator dies.
  void remove_message_queue(std::string_view name) { message_queues_.erase(std::string{name}); }

private:
  // Declared before network_, which borrows it.
  SimRandom random_;
  SimEventLog events_;
  SimScheduler scheduler_;
  SimNetwork network_;

  std::int64_t now_ns_;
  std::int64_t realtime_offset_ns_;

  /// Over-allocated storage plus the aligned view handed out; see
  /// kRegionAlignment for why the two differ.
  struct MappedRegion {
    std::vector<std::byte> storage;
    std::span<std::byte> bytes;
  };

  std::map<std::string, MappedRegion, std::less<>> regions_;
  std::map<std::string, std::vector<std::byte>, std::less<>> files_;
  std::map<std::string, ProcessExit, std::less<>> programs_;
  std::string watchdog_path_;

  // Zero means the filesystem never fills, which is what every test that is
  // not about a full disk wants.
  std::uint64_t file_system_capacity_bytes_ = 0;

  // Above kSelfProcessId, so children and the simulation itself stay distinct.
  std::int32_t next_process_id_ = kSelfProcessId + 1;
  std::map<std::int32_t, bool> processes_alive_;
  std::map<std::string, MessageQueueState, std::less<>> message_queues_;
};

} // namespace volt::pal::sim::detail
