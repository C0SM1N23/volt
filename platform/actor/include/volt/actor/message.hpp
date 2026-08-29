#pragma once

#include "payload_view.hpp"
#include "timestamp.hpp"
#include "topic_id.hpp"

#include <type_traits>

namespace volt {

/// Describes one immutable message presented to an actor.
///
/// The sender owns `payload` and keeps its bytes alive and unmoved until the
/// dispatcher has completed `on_message`. P12 supplies that ownership from its
/// shared-memory slots; P11 deliberately stores only the view.
struct Message {
  Timestamp timestamp{};
  TopicId topic{};
  PayloadView payload{};
};

static_assert(std::is_trivially_copyable_v<Message>);

} // namespace volt
