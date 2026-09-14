#pragma once

#include "volt/ipc/detail/topic_state.hpp"
#include "volt/ipc/topic_config.hpp"
#include "volt/pal/platform.hpp"

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace volt::ipc {

template <typename T> class Publisher;
template <typename T> class Subscriber;

namespace detail {

/// What a topic's endpoints share: the mapping that owns the bytes, the
/// engine over them, and the platform that answers liveness questions.
///
/// Boxed once on the heap so that moving a `Topic` moves one pointer while
/// every outstanding endpoint keeps aiming at valid state.
struct TopicCore {
  pal::IPlatform *platform = nullptr;
  std::unique_ptr<pal::ISharedMemory> memory;
  TopicState state;

  /// Reclaims whatever dead processes left behind, with the platform as the
  /// judge of dead. Runs before every seat claim, so a crashed tenant's seat
  /// is taken over instead of blocking its successor.
  void recover() noexcept {
    state.recover([this](std::int32_t process) { return platform->process_alive(process); });
  }
};

/// The payload contract, spelled once: it must be safe to place in shared
/// memory and to read from another process. Trivially copyable so bytes are
/// the whole story, standard layout so both sides agree what the bytes mean.
/// Default member initializers stay allowed - SPEC 10.2's own example uses
/// them - because an aggregate is an implicit-lifetime type either way. No
/// pointers survive an address space change, which no trait can check; that
/// part stays in the type's documentation.
template <typename T>
concept SharablePayload = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

/// The one place bytes in the segment become a typed payload.
///
/// The slot is zero-filled at creation, `T` is an implicit-lifetime type by
/// the concept above, and every write travels through this same window.
template <typename T> [[nodiscard]] T *payload_as(TopicState &state, std::uint32_t index) noexcept {
  return reinterpret_cast<T *>(state.payload_at(index));
}

} // namespace detail

/// One named zero-copy channel: a shared segment holding a pool of `T`
/// messages, one publisher seat and up to `max_subscribers` consumer seats
/// (SPEC 10.2).
///
/// The topic object is the segment's local handle. Endpoints created from it
/// borrow it, so it must outlive its publishers, subscribers, loans and
/// samples in this process.
template <typename T> class Topic final {
  static_assert(detail::SharablePayload<T>,
                "a shared-memory payload must be trivially copyable and standard layout");

public:
  /// Creates the named segment, replacing any previous one, and becomes its
  /// layout authority.
  [[nodiscard]] static core::expected<Topic> create(pal::IPlatform &platform, std::string_view name,
                                                    const TopicConfig &config) noexcept {
    const core::expected<std::size_t> bytes =
        detail::TopicState::required_bytes(config, sizeof(T), alignof(T));
    if (!bytes.has_value()) {
      return std::unexpected{bytes.error()};
    }
    core::expected<std::unique_ptr<pal::ISharedMemory>> memory =
        platform.create_shared_memory(name, *bytes);
    if (!memory.has_value()) {
      return std::unexpected{memory.error()};
    }
    core::expected<detail::TopicState> state =
        detail::TopicState::create_in((*memory)->bytes(), config, sizeof(T), alignof(T));
    if (!state.has_value()) {
      return std::unexpected{state.error()};
    }
    return Topic{platform, std::move(*memory), *state};
  }

  /// Attaches to a segment another process created.
  ///
  /// @errors kResourceUnavailable when no such segment exists or it is not a
  ///         finished topic, kConfigInvalidValue when it was built for a
  ///         different payload shape or configuration
  [[nodiscard]] static core::expected<Topic> open(pal::IPlatform &platform,
                                                  std::string_view name) noexcept {
    core::expected<std::unique_ptr<pal::ISharedMemory>> memory = platform.open_shared_memory(name);
    if (!memory.has_value()) {
      return std::unexpected{memory.error()};
    }
    core::expected<detail::TopicState> state =
        detail::TopicState::open_in((*memory)->bytes(), sizeof(T), alignof(T));
    if (!state.has_value()) {
      return std::unexpected{state.error()};
    }
    Topic topic{platform, std::move(*memory), *state};
    // Joining is the natural moment to sweep up after a crash: the segment
    // may be older than every live process using it.
    topic.core_->recover();
    return topic;
  }

  /// Claims the producer seat.
  /// @errors kResourceBusy while another live process publishes here
  [[nodiscard]] core::expected<Publisher<T>> publisher() noexcept;

  /// Claims a consumer seat.
  /// @errors kResourceExhausted when every seat belongs to a live process
  [[nodiscard]] core::expected<Subscriber<T>> subscriber() noexcept;

  /// Free message slots right now.
  [[nodiscard]] std::uint64_t available_slots() const noexcept {
    return core_->state.available_slots();
  }

  /// Attached consumer seats right now.
  [[nodiscard]] std::uint32_t subscriber_count() const noexcept {
    return core_->state.subscriber_count();
  }

  /// Sweeps seats owned by dead processes. Claims do this on their own; the
  /// entry point exists for supervision loops that want reclamation without
  /// attaching.
  void recover() noexcept { core_->recover(); }

private:
  template <typename U> friend class Publisher;
  template <typename U> friend class Subscriber;

  Topic(pal::IPlatform &platform, std::unique_ptr<pal::ISharedMemory> memory,
        const detail::TopicState &state)
      : core_{std::make_unique<detail::TopicCore>(&platform, std::move(memory), state)} {}

  std::unique_ptr<detail::TopicCore> core_;
};

} // namespace volt::ipc
