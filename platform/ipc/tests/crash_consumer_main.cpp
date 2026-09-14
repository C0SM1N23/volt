// Consumer that takes samples, holds them and never lets go, until a signal
// kills it. The crash-recovery test spawns it, waits until the pool shows
// the held references, and shoots it with SIGKILL: exactly the failure mode
// SPEC 10.2 requires the transport to survive without leaking.

#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"

#include <charconv>
#include <string_view>
#include <vector>

namespace {

/// The payload the crash campaign uses; must match the test's.
struct CrashPayload {
  std::uint64_t sequence = 0;
};

constexpr int kUsageError = 2;
constexpr int kTopicError = 3;

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    return kUsageError;
  }
  const std::string_view name{argv[1]};
  const std::string_view count_text{argv[2]};
  std::uint32_t hold_count = 0;
  const std::from_chars_result parsed =
      std::from_chars(count_text.data(), count_text.data() + count_text.size(), hold_count);
  if (parsed.ec != std::errc{} || hold_count == 0) {
    return kUsageError;
  }

  volt::pal::posix::PosixPlatform platform;
  volt::core::expected<volt::ipc::Topic<CrashPayload>> topic =
      volt::ipc::Topic<CrashPayload>::open(platform, name);
  if (!topic.has_value()) {
    return kTopicError;
  }
  volt::core::expected<volt::ipc::Subscriber<CrashPayload>> subscriber = topic->subscriber();
  if (!subscriber.has_value()) {
    return kTopicError;
  }

  std::vector<volt::ipc::Sample<CrashPayload>> hostages;
  hostages.reserve(hold_count);
  while (hostages.size() < hold_count) {
    volt::core::expected<volt::ipc::Sample<CrashPayload>> sample = subscriber->take();
    if (sample.has_value()) {
      hostages.push_back(std::move(*sample));
    }
  }

  // Holding everything, waiting for the bullet. The nap only spares a CPU,
  // any duration works because the parent kills, never joins.
  constexpr auto kNap = volt::core::Duration::from_ms(50);
  while (true) {
    [[maybe_unused]] const volt::core::expected<void> slept = platform.clock().sleep_for(kNap);
  }
}
