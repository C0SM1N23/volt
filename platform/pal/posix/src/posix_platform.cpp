#include "volt/pal/posix/posix_platform.hpp"

#include "posix_error.hpp"
#include "posix_file.hpp"
#include "posix_process.hpp"
#include "posix_shared_memory.hpp"
#include "posix_socket.hpp"
#include "posix_thread.hpp"
#include "posix_timer.hpp"
#include "posix_watchdog_device.hpp"

#include "volt/core/endian.hpp"

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

namespace volt::pal::posix {
namespace {

// pthread_setname_np accepts 16 bytes including the terminator, so a longer
// name is truncated rather than rejected: a thread that runs with a shortened
// name is better than a service that refuses to start over a label.
constexpr std::size_t kMaxThreadNameLength = 15;

// Owner-writable, world-readable. Logs and traces are meant to be collected by
// a developer, and nothing VOLT writes to a plain file is a secret.
constexpr ::mode_t kCreatedFilePermissions = 0644;

/// Carries the body and the name across the pthread boundary.
struct ThreadContext {
  ThreadEntry entry;
  std::string name;
};

/// Runs a thread body and then releases its context.
///
/// A C++ function is handed to a C callback here. Both linkages use the same
/// calling convention on the Itanium ABI that GCC and Clang follow on Linux,
/// and keeping C++ linkage avoids putting a symbol in the global namespace.
void *thread_trampoline(void *argument) noexcept {
  const std::unique_ptr<ThreadContext> context{static_cast<ThreadContext *>(argument)};
  if (!context->name.empty()) {
    static_cast<void>(::pthread_setname_np(::pthread_self(), context->name.c_str()));
  }
  context->entry();
  return nullptr;
}

/// Releases thread attributes however the creation path exits.
class AttributesGuard final {
public:
  // Rule of five because the object owns a pthread attribute block, which the
  // compiler cannot release on its own. Copying is deleted so it is destroyed
  // exactly once.
  AttributesGuard() noexcept { initialised_ = ::pthread_attr_init(&attributes_) == 0; }
  ~AttributesGuard() noexcept {
    if (initialised_) {
      static_cast<void>(::pthread_attr_destroy(&attributes_));
    }
  }
  AttributesGuard(const AttributesGuard &) = delete;
  AttributesGuard &operator=(const AttributesGuard &) = delete;
  AttributesGuard(AttributesGuard &&) = delete;
  AttributesGuard &operator=(AttributesGuard &&) = delete;

  [[nodiscard]] bool initialised() const noexcept { return initialised_; }
  [[nodiscard]] ::pthread_attr_t &get() noexcept { return attributes_; }

private:
  ::pthread_attr_t attributes_{};
  bool initialised_ = false;
};

[[nodiscard]] core::expected<int> to_posix_policy(SchedulingPolicy policy) noexcept {
  switch (policy) {
  case SchedulingPolicy::kOther:
    return SCHED_OTHER;
  case SchedulingPolicy::kFifo:
    return SCHED_FIFO;
  case SchedulingPolicy::kRoundRobin:
    return SCHED_RR;
  }
  return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
}

/// Rejects a priority the policy cannot express, before any thread exists.
[[nodiscard]] core::expected<int> to_posix_priority(int posix_policy,
                                                    core::Priority priority) noexcept {
  const auto requested = static_cast<int>(priority.value());
  if (posix_policy == SCHED_OTHER) {
    // The time-sharing policy has exactly one priority; anything else means
    // the caller believed it was asking for real-time behaviour.
    if (requested != 0) {
      return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
    }
    return 0;
  }
  const int lowest = ::sched_get_priority_min(posix_policy);
  const int highest = ::sched_get_priority_max(posix_policy);
  if (lowest < 0 || highest < 0 || requested < lowest || requested > highest) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return requested;
}

[[nodiscard]] core::expected<void> apply_scheduling(::pthread_attr_t &attributes,
                                                    const ThreadConfig &config) noexcept {
  const core::expected<int> posix_policy = to_posix_policy(config.policy);
  if (!posix_policy.has_value()) {
    return std::unexpected{posix_policy.error()};
  }
  const core::expected<int> posix_priority = to_posix_priority(*posix_policy, config.priority);
  if (!posix_priority.has_value()) {
    return std::unexpected{posix_priority.error()};
  }
  if (config.policy == SchedulingPolicy::kOther) {
    return {};
  }

  // Without EXPLICIT_SCHED the attributes are ignored and the thread quietly
  // inherits the creator's policy, which is exactly the silent failure
  // SPEC 42.2 forbids.
  if (::pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED) != 0 ||
      ::pthread_attr_setschedpolicy(&attributes, *posix_policy) != 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  ::sched_param parameters{};
  parameters.sched_priority = *posix_priority;
  if (::pthread_attr_setschedparam(&attributes, &parameters) != 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

/// Adds one CPU to the set when the mask selects it.
///
/// Split out of the loop below because CPU_SET is a macro that expands to a
/// braced block, and nesting it inside a loop and a condition puts ordinary
/// code past the nesting limit of AGENTS.md 3.11.
void select_cpu(::cpu_set_t &cpus, unsigned cpu, CpuMask mask) noexcept {
  if ((mask & (CpuMask{1} << cpu)) == 0) {
    return;
  }
  CPU_SET(cpu, &cpus);
}

[[nodiscard]] core::expected<void> apply_affinity(::pthread_attr_t &attributes,
                                                  CpuMask mask) noexcept {
  if (mask == 0) {
    return {};
  }
  constexpr unsigned kSelectableCpus = sizeof(CpuMask) * static_cast<unsigned>(core::kBitsPerByte);
  ::cpu_set_t cpus;
  CPU_ZERO(&cpus);
  for (unsigned cpu = 0; cpu < CPU_SETSIZE && cpu < kSelectableCpus; ++cpu) {
    select_cpu(cpus, cpu, mask);
  }
  if (::pthread_attr_setaffinity_np(&attributes, sizeof(cpus), &cpus) != 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  return {};
}

[[nodiscard]] std::string to_shm_name(std::string_view name) noexcept {
  // POSIX wants a single leading slash and nothing that looks like a path.
  std::string normalised;
  normalised.reserve(name.size() + 1);
  normalised.push_back('/');
  for (const char character : name) {
    normalised.push_back(character == '/' ? '_' : character);
  }
  return normalised;
}

[[nodiscard]] core::expected<std::span<std::byte>> map_region(int descriptor,
                                                              std::size_t bytes) noexcept {
  void *address = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  if (address == MAP_FAILED) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return std::span<std::byte>{static_cast<std::byte *>(address), bytes};
}

} // namespace

IClock &PosixPlatform::clock() noexcept { return clock_; }

core::expected<std::unique_ptr<IThread>> PosixPlatform::create_thread(const ThreadConfig &config,
                                                                      ThreadEntry entry) noexcept {
  AttributesGuard guard;
  if (!guard.initialised()) {
    return std::unexpected{core::ErrorCode::kResourceExhausted};
  }
  if (config.stack_bytes > 0 &&
      ::pthread_attr_setstacksize(&guard.get(), config.stack_bytes) != 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }

  const core::expected<void> scheduling = apply_scheduling(guard.get(), config);
  if (!scheduling.has_value()) {
    return std::unexpected{scheduling.error()};
  }
  const core::expected<void> affinity = apply_affinity(guard.get(), config.cpu_mask);
  if (!affinity.has_value()) {
    return std::unexpected{affinity.error()};
  }

  std::string name{config.name.substr(0, std::min(config.name.size(), kMaxThreadNameLength))};
  auto context = std::make_unique<ThreadContext>(ThreadContext{std::move(entry), name});

  ::pthread_t handle{};
  const int result = ::pthread_create(&handle, &guard.get(), thread_trampoline, context.get());
  if (result != 0) {
    return std::unexpected{detail::from_errno(result)};
  }
  // The thread owns the context from here on; releasing after a successful
  // create is what keeps the failure path from leaking it.
  [[maybe_unused]] ThreadContext *const owned_by_thread = context.release();
  return std::make_unique<PosixThread>(handle, std::move(name));
}

core::expected<std::unique_ptr<ITimer>> PosixPlatform::create_timer() noexcept {
  detail::FileDescriptor descriptor{::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return std::make_unique<PosixTimer>(std::move(descriptor));
}

core::expected<std::unique_ptr<ISharedMemory>>
PosixPlatform::create_shared_memory(std::string_view name, std::size_t bytes) noexcept {
  if (bytes == 0) {
    return std::unexpected{core::ErrorCode::kConfigValueOutOfRange};
  }
  const std::string normalised = to_shm_name(name);
  // A leftover region from a crashed run would keep its old size and contents,
  // so the name is removed before it is created again.
  static_cast<void>(::shm_unlink(normalised.c_str()));

  detail::FileDescriptor descriptor{
      ::shm_open(normalised.c_str(), O_CREAT | O_EXCL | O_RDWR, kCreatedFilePermissions)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  if (::ftruncate(descriptor.get(), static_cast<::off_t>(bytes)) != 0) {
    const core::ErrorCode reason = detail::from_errno(errno);
    static_cast<void>(::shm_unlink(normalised.c_str()));
    return std::unexpected{reason};
  }

  core::expected<std::span<std::byte>> mapping = map_region(descriptor.get(), bytes);
  if (!mapping.has_value()) {
    static_cast<void>(::shm_unlink(normalised.c_str()));
    return std::unexpected{mapping.error()};
  }
  return std::make_unique<PosixSharedMemory>(normalised, *mapping,
                                             PosixSharedMemory::Ownership::kCreator);
}

core::expected<std::unique_ptr<ISharedMemory>>
PosixPlatform::open_shared_memory(std::string_view name) noexcept {
  const std::string normalised = to_shm_name(name);
  detail::FileDescriptor descriptor{::shm_open(normalised.c_str(), O_RDWR, 0)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  struct ::stat status{};
  if (::fstat(descriptor.get(), &status) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }

  core::expected<std::span<std::byte>> mapping =
      map_region(descriptor.get(), static_cast<std::size_t>(status.st_size));
  if (!mapping.has_value()) {
    return std::unexpected{mapping.error()};
  }
  return std::make_unique<PosixSharedMemory>(normalised, *mapping,
                                             PosixSharedMemory::Ownership::kOpener);
}

core::expected<std::unique_ptr<ISocket>> PosixPlatform::create_datagram_socket() noexcept {
  detail::FileDescriptor descriptor{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return std::make_unique<PosixSocket>(std::move(descriptor));
}

core::expected<std::unique_ptr<IFile>> PosixPlatform::open_file(std::string_view path,
                                                                FileMode mode) noexcept {
  int flags = O_CLOEXEC;
  switch (mode) {
  case FileMode::kRead:
    flags |= O_RDONLY;
    break;
  case FileMode::kWrite:
    flags |= O_WRONLY | O_CREAT | O_TRUNC;
    break;
  case FileMode::kAppend:
    flags |= O_WRONLY | O_CREAT | O_APPEND;
    break;
  }

  const std::string path_text{path};
  detail::FileDescriptor descriptor{::open(path_text.c_str(), flags, kCreatedFilePermissions)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return std::make_unique<PosixFile>(std::move(descriptor), mode);
}

core::expected<std::unique_ptr<IProcess>>
PosixPlatform::spawn_process(const ProcessConfig &config) noexcept {
  const std::string executable{config.executable};

  // posix_spawn takes a null-terminated argv of writable pointers, so the
  // strings are copied into storage that outlives the call.
  std::vector<std::string> storage;
  storage.reserve(config.arguments.size() + 1);
  storage.emplace_back(executable);
  for (const std::string_view argument : config.arguments) {
    storage.emplace_back(argument);
  }

  std::vector<char *> argv;
  argv.reserve(storage.size() + 1);
  for (std::string &argument : storage) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  ::pid_t identifier = 0;
  const int result =
      ::posix_spawn(&identifier, executable.c_str(), nullptr, nullptr, argv.data(), environ);
  if (result != 0) {
    return std::unexpected{detail::from_errno(result)};
  }
  return std::make_unique<PosixProcess>(identifier);
}

core::expected<std::unique_ptr<IWatchdogDevice>>
PosixPlatform::open_watchdog(std::string_view path) noexcept {
  const std::string path_text{path};
  detail::FileDescriptor descriptor{::open(path_text.c_str(), O_WRONLY | O_CLOEXEC)};
  if (!descriptor.valid()) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return std::make_unique<PosixWatchdogDevice>(std::move(descriptor));
}

core::expected<void> PosixPlatform::lock_memory() noexcept {
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

core::expected<void>
PosixPlatform::set_current_thread_scheduling(SchedulingPolicy policy,
                                             core::Priority priority) noexcept {
  const core::expected<int> posix_policy = to_posix_policy(policy);
  if (!posix_policy.has_value()) {
    return std::unexpected{posix_policy.error()};
  }
  const core::expected<int> posix_priority = to_posix_priority(*posix_policy, priority);
  if (!posix_priority.has_value()) {
    return std::unexpected{posix_priority.error()};
  }

  ::sched_param parameters{};
  parameters.sched_priority = *posix_priority;
  // A zero pid means the calling thread on Linux, which is what a thread
  // promoting itself to real-time needs.
  if (::sched_setscheduler(0, *posix_policy, &parameters) != 0) {
    return std::unexpected{detail::from_errno(errno)};
  }
  return {};
}

} // namespace volt::pal::posix
