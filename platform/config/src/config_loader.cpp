#include "volt/config/config_loader.hpp"

#include "report_writer.hpp"
#include "schema.hpp"

#include "volt/config/lease_config.hpp"
#include "volt/config/node_role.hpp"
#include "volt/config/ptp_config.hpp"
#include "volt/config/replication_config.hpp"
#include "volt/config/scheduler_config.hpp"
#include "volt/config/service_config.hpp"

#include "volt/core/error_code.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::config {
namespace {

[[nodiscard]] std::size_t source_line(const YAML::Node &node) {
  const YAML::Mark mark = node.Mark();
  return mark.is_null() ? 0U : static_cast<std::size_t>(mark.line) + 1U;
}

[[nodiscard]] std::int64_t integer(const YAML::Node &node) { return node.as<std::int64_t>(); }

[[nodiscard]] bool boolean(const YAML::Node &node) { return node.Scalar() == "true"; }

[[nodiscard]] NodeRole node_role(std::string_view value) noexcept {
  return value == "COMPUTE" ? NodeRole::kCompute : NodeRole::kSafety;
}

[[nodiscard]] PtpRole ptp_role(std::string_view value) noexcept {
  if (value == "MASTER") {
    return PtpRole::kMaster;
  }
  return value == "FOLLOWER" ? PtpRole::kFollower : PtpRole::kAuto;
}

[[nodiscard]] ServiceIsolation isolation(std::string_view value) noexcept {
  if (value == "PROCESS") {
    return ServiceIsolation::kProcess;
  }
  return value == "PARTITION" ? ServiceIsolation::kPartition : ServiceIsolation::kThread;
}

[[nodiscard]] ServiceCriticality criticality(std::string_view value) noexcept {
  if (value == "MEDIUM") {
    return ServiceCriticality::kMedium;
  }
  if (value == "HIGH") {
    return ServiceCriticality::kHigh;
  }
  return value == "SAFETY_CRITICAL" ? ServiceCriticality::kSafetyCritical
                                    : ServiceCriticality::kBestEffort;
}

[[nodiscard]] LeaseClass lease_class(std::string_view value) noexcept {
  return value == "FENCED" ? LeaseClass::kFenced : LeaseClass::kUnfenced;
}

[[nodiscard]] ReplicationMode replication_mode(std::string_view value) noexcept {
  return value == "ACTIVE_STANDBY" ? ReplicationMode::kActiveStandby : ReplicationMode::kNone;
}

[[nodiscard]] std::vector<NodeName> decode_node_names(const YAML::Node &sequence) {
  std::vector<NodeName> result;
  result.reserve(sequence.size());
  for (const YAML::Node &entry : sequence) {
    result.emplace_back(entry.Scalar());
  }
  return result;
}

[[nodiscard]] std::vector<CpuId> decode_cpu_ids(const YAML::Node &sequence) {
  std::vector<CpuId> result;
  result.reserve(sequence.size());
  for (const YAML::Node &entry : sequence) {
    result.push_back(CpuId{static_cast<std::uint16_t>(integer(entry))});
  }
  return result;
}

[[nodiscard]] RaftConfig decode_raft(const YAML::Node &node) {
  const YAML::Node bounds = node["election_timeout_ms"];
  return RaftConfig{volt::core::Duration::from_ms(integer(bounds[0])),
                    volt::core::Duration::from_ms(integer(bounds[1])),
                    volt::core::Duration::from_ms(integer(node["heartbeat_ms"]))};
}

[[nodiscard]] SwimConfig decode_swim(const YAML::Node &node) {
  return SwimConfig{volt::core::Duration::from_ms(integer(node["period_ms"])),
                    volt::core::Duration::from_ms(integer(node["timeout_ms"])),
                    static_cast<std::uint16_t>(integer(node["indirect_k"])),
                    volt::core::Duration::from_ms(integer(node["indirect_ms"])),
                    volt::core::Duration::from_ms(integer(node["suspect_ms"]))};
}

[[nodiscard]] CanHeartbeatConfig decode_can_heartbeat(const YAML::Node &node) {
  return CanHeartbeatConfig{CanFrameId{static_cast<std::uint32_t>(integer(node["id"]))},
                            volt::core::Duration::from_ms(integer(node["period_ms"])),
                            static_cast<std::uint16_t>(integer(node["miss_threshold"]))};
}

[[nodiscard]] PtpConfig decode_ptp(const YAML::Node &node) {
  return PtpConfig{ptp_role(node["role"].Scalar()),
                   volt::core::Duration::from_ms(integer(node["sync_interval_ms"])),
                   volt::core::Duration::from_us(integer(node["max_offset_us"]))};
}

[[nodiscard]] ClusterConfig decode_cluster(const YAML::Node &node) {
  return ClusterConfig{decode_node_names(node["peers"]), decode_raft(node["raft"]),
                       decode_swim(node["swim"]), decode_can_heartbeat(node["can_heartbeat"]),
                       decode_ptp(node["ptp"])};
}

[[nodiscard]] NodeIdentityConfig decode_identity(const YAML::Node &node) {
  std::vector<NodeRole> roles;
  roles.reserve(node["role"].size());
  for (const YAML::Node &entry : node["role"]) {
    roles.push_back(node_role(entry.Scalar()));
  }
  const YAML::Node interfaces = node["interfaces"];
  const YAML::Node realtime = node["rt"];
  return NodeIdentityConfig{NodeName{node["id"].Scalar()}, std::move(roles),
                            NodeInterfaces{interfaces["can"].Scalar(), interfaces["eth"].Scalar(),
                                           interfaces["diag_eth"].Scalar()},
                            RealtimeConfig{decode_cpu_ids(realtime["isolated_cpus"]),
                                           boolean(realtime["mlockall"]),
                                           boolean(realtime["sched_reset_on_fork"])}};
}

[[nodiscard]] SchedulerConfig decode_scheduler(const YAML::Node &node) {
  return SchedulerConfig{SchedulerPolicy::kFifo, volt::core::Priority{static_cast<std::uint8_t>(
                                                     integer(node["priority"]))}};
}

[[nodiscard]] LeaseConfig decode_lease(const YAML::Node &node) {
  return LeaseConfig{node["resource"].Scalar(),
                     lease_class(node["class"].Scalar()),
                     volt::core::Duration::from_ms(integer(node["duration_ms"])),
                     volt::core::Duration::from_ms(integer(node["renew_ms"])),
                     volt::core::Duration::from_ms(integer(node["self_yield_ms"])),
                     NodeName{node["preauthorized_successor"].Scalar()}};
}

[[nodiscard]] ReplicationConfig decode_replication(const YAML::Node &node) {
  if (!node) {
    return {};
  }
  if (node.IsScalar()) {
    return ReplicationConfig{replication_mode(node.Scalar()), std::nullopt, {}};
  }
  return ReplicationConfig{replication_mode(node["mode"].Scalar()),
                           volt::core::Duration::from_us(integer(node["sync_period_us"])),
                           decode_node_names(node["standby_on"])};
}

[[nodiscard]] ServiceConfig decode_service(const YAML::Node &node) {
  ServiceConfig result{node["name"].Scalar(),
                       isolation(node["isolation"].Scalar()),
                       criticality(node["criticality"].Scalar()),
                       volt::core::Duration::from_us(integer(node["period_us"])),
                       decode_cpu_ids(node["cpu"]),
                       std::nullopt,
                       std::nullopt,
                       decode_replication(node["replication"])};
  if (node["sched"]) {
    result.scheduler = decode_scheduler(node["sched"]);
  }
  if (node["lease"]) {
    result.lease = decode_lease(node["lease"]);
  }
  return result;
}

[[nodiscard]] std::vector<ServiceConfig> decode_services(const YAML::Node &sequence) {
  std::vector<ServiceConfig> result;
  result.reserve(sequence.size());
  for (const YAML::Node &entry : sequence) {
    result.push_back(decode_service(entry));
  }
  return result;
}

[[nodiscard]] NodeConfig decode_node_config(const YAML::Node &root) {
  const YAML::Node telemetry = root["telemetry"];
  return NodeConfig{
      decode_identity(root["node"]), decode_cluster(root["cluster"]),
      decode_services(root["services"]),
      TelemetryConfig{boolean(telemetry["trace"]),
                      NetworkPort{static_cast<std::uint16_t>(integer(telemetry["metrics_port"]))},
                      boolean(telemetry["record"])}};
}

struct SemanticFailure final {
  std::string field;
  std::string found;
  std::string expected;
  volt::core::ErrorCode code;
};

[[nodiscard]] volt::core::ErrorCode semantic_error(LoadReport &report, std::string_view file,
                                                   const YAML::Node &node,
                                                   SemanticFailure failure) {
  detail::ReportWriter writer{report};
  return writer.fail(ConfigDiagnostic{failure.code, std::string{file}, source_line(node),
                                      std::move(failure.field), std::move(failure.found),
                                      std::move(failure.expected)});
}

[[nodiscard]] volt::expected<void> validate_peer_uniqueness(const YAML::Node &root,
                                                            const ClusterConfig &config,
                                                            std::string_view file,
                                                            LoadReport &report) {
  std::set<std::string_view, std::less<>> peers;
  for (std::size_t index = 0; index < config.peers.size(); ++index) {
    const std::string_view name = config.peers[index].value();
    if (peers.insert(name).second) {
      continue;
    }
    const YAML::Node duplicate = root["peers"][index];
    const auto code = semantic_error(
        report, file, duplicate,
        {"peers[" + std::to_string(index) + "]", "duplicate node '" + std::string{name} + "'",
         "unique peer node", volt::core::ErrorCode::kConfigDuplicateId});
    return std::unexpected{code};
  }
  return {};
}

[[nodiscard]] volt::expected<void> validate_election_bounds(const YAML::Node &root,
                                                            const ClusterConfig &config,
                                                            std::string_view file,
                                                            LoadReport &report) {
  if (config.raft.election_timeout_min < config.raft.election_timeout_max) {
    return {};
  }
  const YAML::Node upper = root["raft"]["election_timeout_ms"][1];
  const auto code = semantic_error(
      report, file, upper,
      {"raft.election_timeout_ms[1]", "upper bound not greater than lower bound",
       "upper bound greater than lower bound", volt::core::ErrorCode::kConfigInvalidValue});
  return std::unexpected{code};
}

[[nodiscard]] volt::expected<void> validate_cluster_semantics(const YAML::Node &root,
                                                              const ClusterConfig &config,
                                                              std::string_view file,
                                                              LoadReport &report) {
  const auto peer_result = validate_peer_uniqueness(root, config, file, report);
  if (!peer_result.has_value()) {
    return peer_result;
  }
  return validate_election_bounds(root, config, file, report);
}

[[nodiscard]] volt::expected<void> validate_service_names(const YAML::Node &root,
                                                          const NodeConfig &config,
                                                          std::string_view file,
                                                          LoadReport &report) {
  std::set<std::string_view, std::less<>> names;
  for (std::size_t index = 0; index < config.services.size(); ++index) {
    const std::string_view name = config.services[index].name;
    if (names.insert(name).second) {
      continue;
    }
    const YAML::Node duplicate = root["services"][index]["name"];
    const auto code =
        semantic_error(report, file, duplicate,
                       {"services[" + std::to_string(index) + "].name",
                        "duplicate service '" + std::string{name} + "'", "unique service name",
                        volt::core::ErrorCode::kConfigDuplicateId});
    return std::unexpected{code};
  }
  return {};
}

} // namespace

volt::expected<NodeConfig> load_node_config(std::string_view path, LoadReport &report) {
  const auto root_result = detail::load_validated_yaml(path, detail::node_schema(), report);
  if (!root_result.has_value()) {
    return std::unexpected{root_result.error()};
  }
  const YAML::Node &root = *root_result;
  NodeConfig config = decode_node_config(root);
  const auto cluster_result =
      validate_cluster_semantics(root["cluster"], config.cluster, path, report);
  if (!cluster_result.has_value()) {
    return std::unexpected{cluster_result.error()};
  }
  const auto service_result = validate_service_names(root, config, path, report);
  if (!service_result.has_value()) {
    return std::unexpected{service_result.error()};
  }
  return config;
}

volt::expected<ClusterConfig> load_cluster_config(std::string_view path, LoadReport &report) {
  const auto root_result = detail::load_validated_yaml(path, detail::cluster_schema(), report);
  if (!root_result.has_value()) {
    return std::unexpected{root_result.error()};
  }
  const YAML::Node &root = *root_result;
  ClusterConfig config = decode_cluster(root);
  const auto semantic_result = validate_cluster_semantics(root, config, path, report);
  if (!semantic_result.has_value()) {
    return std::unexpected{semantic_result.error()};
  }
  return config;
}

} // namespace volt::config
