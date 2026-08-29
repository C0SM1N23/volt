#include "volt/config/config_hash.hpp"

#include "volt/config/lease_config.hpp"
#include "volt/config/node_role.hpp"
#include "volt/config/ptp_config.hpp"
#include "volt/config/replication_config.hpp"
#include "volt/config/scheduler_config.hpp"
#include "volt/config/service_config.hpp"

#include "volt/core/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace volt::config {
namespace {

// This byte versions the canonical byte layout defined in this file. Incrementing it deliberately
// invalidates hashes from older layouts, which prevents two schema versions joining as equals.
constexpr std::uint8_t kCanonicalFormatVersion = 1;

class CanonicalBytes final {
public:
  void append_bool(bool value) { append_unsigned(static_cast<std::uint8_t>(value)); }

  template <typename Enum> void append_enum(Enum value) {
    append_unsigned(static_cast<std::underlying_type_t<Enum>>(value));
  }

  template <typename Unsigned> void append_unsigned(Unsigned value) {
    static_assert(std::is_unsigned_v<Unsigned>);
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
      const auto shift = static_cast<unsigned>(index * volt::core::kBitsPerByte);
      bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift)));
    }
  }

  void append_duration(volt::core::Duration duration) {
    append_unsigned(static_cast<std::uint64_t>(duration.ns()));
  }

  void append_string(std::string_view value) {
    append_unsigned(static_cast<std::uint64_t>(value.size()));
    for (const char character : value) {
      bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(character)));
    }
  }

  [[nodiscard]] std::span<const std::byte> view() const noexcept { return bytes_; }

private:
  std::vector<std::byte> bytes_;
};

void append_node_names(CanonicalBytes &bytes, const std::vector<NodeName> &names) {
  bytes.append_unsigned(static_cast<std::uint64_t>(names.size()));
  for (const NodeName &name : names) {
    bytes.append_string(name.value());
  }
}

void append_cluster(CanonicalBytes &bytes, const ClusterConfig &config) {
  append_node_names(bytes, config.peers);
  bytes.append_duration(config.raft.election_timeout_min);
  bytes.append_duration(config.raft.election_timeout_max);
  bytes.append_duration(config.raft.heartbeat_period);
  bytes.append_duration(config.swim.period);
  bytes.append_duration(config.swim.timeout);
  bytes.append_unsigned(config.swim.indirect_probe_count);
  bytes.append_duration(config.swim.indirect_timeout);
  bytes.append_duration(config.swim.suspect_timeout);
  bytes.append_unsigned(config.can_heartbeat.frame_id.value());
  bytes.append_duration(config.can_heartbeat.period);
  bytes.append_unsigned(config.can_heartbeat.miss_threshold);
  bytes.append_enum(config.ptp.role);
  bytes.append_duration(config.ptp.sync_interval);
  bytes.append_duration(config.ptp.maximum_offset);
}

void append_node_identity(CanonicalBytes &bytes, const NodeIdentityConfig &config) {
  bytes.append_string(config.id.value());
  bytes.append_unsigned(static_cast<std::uint64_t>(config.roles.size()));
  for (const NodeRole role : config.roles) {
    bytes.append_enum(role);
  }
  bytes.append_string(config.interfaces.can);
  bytes.append_string(config.interfaces.ethernet);
  bytes.append_string(config.interfaces.diagnostic_ethernet);
  bytes.append_unsigned(static_cast<std::uint64_t>(config.realtime.isolated_cpu_ids.size()));
  for (const CpuId cpu_id : config.realtime.isolated_cpu_ids) {
    bytes.append_unsigned(cpu_id.value());
  }
  bytes.append_bool(config.realtime.lock_memory);
  bytes.append_bool(config.realtime.reset_scheduler_on_fork);
}

void append_scheduler(CanonicalBytes &bytes, const std::optional<SchedulerConfig> &scheduler) {
  bytes.append_bool(scheduler.has_value());
  if (!scheduler.has_value()) {
    return;
  }
  bytes.append_enum(scheduler->policy);
  bytes.append_unsigned(scheduler->priority.value());
}

void append_lease(CanonicalBytes &bytes, const std::optional<LeaseConfig> &lease) {
  bytes.append_bool(lease.has_value());
  if (!lease.has_value()) {
    return;
  }
  bytes.append_string(lease->resource);
  bytes.append_enum(lease->lease_class);
  bytes.append_duration(lease->duration);
  bytes.append_duration(lease->renew_period);
  bytes.append_duration(lease->self_yield_period);
  bytes.append_string(lease->preauthorized_successor.value());
}

void append_replication(CanonicalBytes &bytes, const ReplicationConfig &replication) {
  bytes.append_enum(replication.mode);
  bytes.append_bool(replication.sync_period.has_value());
  if (replication.sync_period.has_value()) {
    bytes.append_duration(*replication.sync_period);
  }
  append_node_names(bytes, replication.standby_nodes);
}

void append_service(CanonicalBytes &bytes, const ServiceConfig &service) {
  bytes.append_string(service.name);
  bytes.append_enum(service.isolation);
  bytes.append_enum(service.criticality);
  bytes.append_duration(service.period);
  bytes.append_unsigned(static_cast<std::uint64_t>(service.cpu_ids.size()));
  for (const CpuId cpu_id : service.cpu_ids) {
    bytes.append_unsigned(cpu_id.value());
  }
  append_scheduler(bytes, service.scheduler);
  append_lease(bytes, service.lease);
  append_replication(bytes, service.replication);
}

void append_services(CanonicalBytes &bytes, const std::vector<ServiceConfig> &services) {
  bytes.append_unsigned(static_cast<std::uint64_t>(services.size()));
  for (const ServiceConfig &service : services) {
    append_service(bytes, service);
  }
}

} // namespace

std::uint64_t config_hash(const ClusterConfig &config) {
  CanonicalBytes bytes;
  bytes.append_unsigned(kCanonicalFormatVersion);
  append_cluster(bytes, config);
  return volt::core::xxhash64(bytes.view());
}

std::uint64_t config_hash(const NodeConfig &config) {
  CanonicalBytes bytes;
  bytes.append_unsigned(kCanonicalFormatVersion);
  append_node_identity(bytes, config.node);
  append_cluster(bytes, config.cluster);
  append_services(bytes, config.services);
  bytes.append_bool(config.telemetry.trace_enabled);
  bytes.append_unsigned(config.telemetry.metrics_port.value());
  bytes.append_bool(config.telemetry.recording_enabled);
  return volt::core::xxhash64(bytes.view());
}

} // namespace volt::config
