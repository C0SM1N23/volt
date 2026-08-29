#include "volt/config/config_hash.hpp"
#include "volt/config/config_loader.hpp"
#include "volt/config/config_reloader.hpp"
#include "volt/config/lease_config.hpp"
#include "volt/config/node_role.hpp"
#include "volt/config/replication_config.hpp"
#include "volt/config/service_config.hpp"

#include "volt/core/duration.hpp"
#include "volt/core/error_code.hpp"

#include <yaml-cpp/yaml.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::config {
namespace {

[[nodiscard]] std::string bad_config_path(std::string_view file) {
  return std::string{VOLT_CONFIG_TEST_DATA} + "/bad_configs/" + std::string{file};
}

[[nodiscard]] std::string data_path(std::string_view file) {
  return std::string{VOLT_CONFIG_TEST_DATA} + "/" + std::string{file};
}

void collect_mapping_fields(const YAML::Node &node, std::string_view path,
                            std::vector<std::string> &fields) {
  if (node.IsMap()) {
    for (const auto &entry : node) {
      const std::string name = entry.first.Scalar();
      const std::string child = path.empty() ? name : std::string{path} + "." + name;
      fields.push_back(child);
      collect_mapping_fields(entry.second, child, fields);
    }
    return;
  }
  if (node.IsSequence()) {
    for (std::size_t index = 0; index < node.size(); ++index) {
      collect_mapping_fields(node[index], std::string{path} + "[" + std::to_string(index) + "]",
                             fields);
    }
  }
}

[[nodiscard]] std::vector<std::string> fields_in_file(std::string_view path) {
  const YAML::Node root = YAML::LoadFile(std::string{path});
  std::vector<std::string> fields;
  collect_mapping_fields(root, {}, fields);
  std::ranges::sort(fields);
  return fields;
}

TEST(ConfigLoader, LoadsCompleteAnnexBNodeConfiguration) {
  LoadReport report;
  const volt::expected<NodeConfig> result = load_node_config(VOLT_CONFIG_EXAMPLE, report);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->node.id.value(), "NodeA");
  EXPECT_EQ(result->node.roles, (std::vector{NodeRole::kCompute, NodeRole::kSafety}));
  EXPECT_EQ(result->cluster.peers, (std::vector{NodeName{"NodeB"}, NodeName{"NodeC"}}));
  ASSERT_EQ(result->services.size(), 5U);
  EXPECT_EQ(result->services[2].criticality, ServiceCriticality::kSafetyCritical);
  EXPECT_EQ(result->services[2].lease.transform(
                [](const LeaseConfig &lease) { return lease.lease_class; }),
            LeaseClass::kFenced);
  EXPECT_EQ(result->services[2].replication.mode, ReplicationMode::kActiveStandby);
  EXPECT_EQ(result->telemetry.metrics_port.value(), 9101U);
  EXPECT_FALSE(report.diagnostic().has_value());
  EXPECT_EQ(report.error_count(), 0U);
}

TEST(ConfigLoader, ConsumesEveryFieldInAnnexBExample) {
  LoadReport report;
  const volt::expected<NodeConfig> result = load_node_config(VOLT_CONFIG_EXAMPLE, report);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(report.consumed_fields(), fields_in_file(VOLT_CONFIG_EXAMPLE));
}

TEST(ConfigLoader, LoadsStandaloneClusterConfiguration) {
  LoadReport report;
  const volt::expected<ClusterConfig> result = load_cluster_config(VOLT_CLUSTER_EXAMPLE, report);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->peers.size(), 2U);
  EXPECT_EQ(result->raft.heartbeat_period, volt::core::Duration::from_ms(50));
  EXPECT_EQ(report.line_for("raft.heartbeat_ms"), 2U);
}

TEST(ConfigHash, IsStableAcrossEquivalentEffectiveConfigurations) {
  LoadReport node_report;
  LoadReport cluster_report;
  const volt::expected<NodeConfig> node = load_node_config(VOLT_CONFIG_EXAMPLE, node_report);
  const volt::expected<ClusterConfig> cluster =
      load_cluster_config(VOLT_CLUSTER_EXAMPLE, cluster_report);

  ASSERT_TRUE(node.has_value());
  ASSERT_TRUE(cluster.has_value());
  EXPECT_EQ(config_hash(node->cluster), config_hash(*cluster));
  NodeConfig changed = *node;
  changed.telemetry.trace_enabled = !changed.telemetry.trace_enabled;
  EXPECT_NE(config_hash(*node), config_hash(changed));
}

TEST(ConfigReloader, PublishesOnlyCalibratableChanges) {
  LoadReport initial_report;
  const volt::expected<NodeConfig> initial = load_node_config(VOLT_CONFIG_EXAMPLE, initial_report);
  ASSERT_TRUE(initial.has_value());
  std::vector<ConfigChange> changes;
  ConfigReloader reloader{*initial,
                          [&changes](const ConfigChange &change) { changes.push_back(change); }};
  LoadReport reload_report;

  const volt::expected<void> result =
      reloader.reload(data_path("node_a_calibration_reload.yaml"), reload_report);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(changes, (std::vector{ConfigChange{"telemetry.trace", "true", "false"},
                                  ConfigChange{"telemetry.record", "false", "true"}}));
  EXPECT_FALSE(reloader.current().telemetry.trace_enabled);
  EXPECT_TRUE(reloader.current().telemetry.recording_enabled);
}

TEST(ConfigReloader, RejectsNonCalibratableChangeBeforeCallback) {
  LoadReport initial_report;
  const volt::expected<NodeConfig> initial = load_node_config(VOLT_CONFIG_EXAMPLE, initial_report);
  ASSERT_TRUE(initial.has_value());
  std::vector<ConfigChange> changes;
  ConfigReloader reloader{*initial,
                          [&changes](const ConfigChange &change) { changes.push_back(change); }};
  LoadReport reload_report;
  const std::string path = data_path("node_a_non_calibration_reload.yaml");

  const volt::expected<void> result = reloader.reload(path, reload_report);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), volt::core::ErrorCode::kConfigInvalidValue);
  const std::string expected_diagnostic =
      path + ":24: field 'telemetry.metrics_port': found '9102'; expected unchanged field "
             "(calibratable: false)";
  EXPECT_EQ(reload_report.diagnostic().transform(
                [](const ConfigDiagnostic &diagnostic) { return diagnostic.message(); }),
            expected_diagnostic);
  EXPECT_TRUE(changes.empty());
  EXPECT_EQ(reloader.current().telemetry.metrics_port.value(), 9101U);
}

enum class LoaderKind : std::uint8_t { kNode, kCluster };

struct InvalidConfigCase final {
  std::string_view file;
  LoaderKind loader;
  volt::core::ErrorCode code;
  std::string_view diagnostic_suffix;
};

constexpr std::array<InvalidConfigCase, 25> kInvalidConfigs{{
    {"01_unknown_root_field.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'nod': found unknown field 'nod'; expected known field: one of [node, cluster, "
     "services, telemetry]"},
    {"02_missing_root_field.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigMissingField,
     ":1: field 'cluster': found <missing>; expected required field"},
    {"03_node_wrong_type.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'node': found 'NodeA'; expected mapping"},
    {"04_node_id_wrong_type.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":2: field 'node.id': found '7'; expected string"},
    {"05_invalid_node_role.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":3: field 'node.role[1]': found 'DRIVER'; expected one of [COMPUTE, SAFETY]"},
    {"06_missing_interface.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigMissingField,
     ":4: field 'node.interfaces.diag_eth': found <missing>; expected required field"},
    {"07_invalid_boolean.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":5: field 'node.rt.mlockall': found 'yes'; expected boolean (true or false)"},
    {"08_cpu_out_of_range.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigValueOutOfRange,
     ":5: field 'node.rt.isolated_cpus[0]': found '1024'; expected integer in range [0, 1023] "
     "CPU index"},
    {"09_empty_peer_list.yaml", LoaderKind::kCluster, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'peers': found <sequence>; expected sequence with 1..3 items"},
    {"10_duplicate_peer.yaml", LoaderKind::kCluster, volt::core::ErrorCode::kConfigDuplicateId,
     ":1: field 'peers[1]': found duplicate node 'NodeB'; expected unique peer node"},
    {"11_election_bound_count.yaml", LoaderKind::kCluster,
     volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'raft.election_timeout_ms': found <sequence>; expected sequence with 2..2 items"},
    {"12_election_bounds_reversed.yaml", LoaderKind::kCluster,
     volt::core::ErrorCode::kConfigInvalidValue,
     ":2: field 'raft.election_timeout_ms[1]': found upper bound not greater than lower bound; "
     "expected upper bound greater than lower bound"},
    {"13_missing_swim_field.yaml", LoaderKind::kCluster, volt::core::ErrorCode::kConfigMissingField,
     ":1: field 'swim.suspect_ms': found <missing>; expected required field"},
    {"14_swim_dependency.yaml", LoaderKind::kCluster, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'swim.timeout_ms': found '3'; expected value less than field 'period_ms'"},
    {"15_can_id_out_of_range.yaml", LoaderKind::kCluster,
     volt::core::ErrorCode::kConfigValueOutOfRange,
     ":1: field 'can_heartbeat.id': found '0x20000000'; expected integer in range [0, 536870911] "
     "CAN identifier"},
    {"16_miss_threshold_wrong_type.yaml", LoaderKind::kCluster,
     volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'can_heartbeat.miss_threshold': found 'three'; expected integer in range [1, "
     "65535] count"},
    {"17_invalid_ptp_role.yaml", LoaderKind::kCluster, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'ptp.role': found 'CLIENT'; expected one of [AUTO, MASTER, FOLLOWER]"},
    {"18_services_wrong_type.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":1: field 'services': found 'BrakeControl'; expected sequence with 1..64 items"},
    {"19_unknown_service_field.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":3: field 'services[0].priorty': found unknown field 'priorty'; expected known field: one "
     "of [name, isolation, criticality, period_us, cpu, sched, lease, replication]"},
    {"20_invalid_isolation.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":2: field 'services[0].isolation': found 'CONTAINER'; expected one of [THREAD, PROCESS, "
     "PARTITION]"},
    {"21_period_out_of_range.yaml", LoaderKind::kNode,
     volt::core::ErrorCode::kConfigValueOutOfRange,
     ":2: field 'services[0].period_us': found '0'; expected integer in range [1, 2147483647] us"},
    {"22_priority_out_of_range.yaml", LoaderKind::kNode,
     volt::core::ErrorCode::kConfigValueOutOfRange,
     ":3: field 'services[0].sched.priority': found '100'; expected integer in range [1, 99] "
     "priority"},
    {"23_scheduler_dependency.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":3: field 'services[0].sched': found <mapping>; expected field 'isolation' equal to "
     "'PARTITION'"},
    {"24_lease_dependency.yaml", LoaderKind::kNode, volt::core::ErrorCode::kConfigInvalidValue,
     ":3: field 'services[0].lease': found <mapping>; expected field 'criticality' equal to "
     "'SAFETY_CRITICAL'"},
    {"25_replication_missing_field.yaml", LoaderKind::kNode,
     volt::core::ErrorCode::kConfigMissingField,
     ":3: field 'services[0].replication.standby_on': found <missing>; expected required field"},
}};

struct LoadOutcome final {
  bool has_value;
  volt::core::ErrorCode error;
};

[[nodiscard]] LoadOutcome load_invalid_node(std::string_view path, LoadReport &report) {
  const volt::expected<NodeConfig> result = load_node_config(path, report);
  return LoadOutcome{result.has_value(), result.has_value()
                                             ? volt::core::ErrorCode::kInternalOutOfRange
                                             : result.error()};
}

[[nodiscard]] LoadOutcome load_invalid_cluster(std::string_view path, LoadReport &report) {
  const volt::expected<ClusterConfig> result = load_cluster_config(path, report);
  return LoadOutcome{result.has_value(), result.has_value()
                                             ? volt::core::ErrorCode::kInternalOutOfRange
                                             : result.error()};
}

[[nodiscard]] LoadOutcome load_invalid(const InvalidConfigCase &test_case, std::string_view path,
                                       LoadReport &report) {
  return test_case.loader == LoaderKind::kNode ? load_invalid_node(path, report)
                                               : load_invalid_cluster(path, report);
}

class InvalidConfigTest : public testing::TestWithParam<InvalidConfigCase> {};

TEST_P(InvalidConfigTest, ReportsExactSchemaDiagnostic) {
  const InvalidConfigCase test_case = GetParam();
  const std::string path = bad_config_path(test_case.file);
  LoadReport report;

  const LoadOutcome outcome = load_invalid(test_case, path, report);

  EXPECT_FALSE(outcome.has_value);
  EXPECT_EQ(outcome.error, test_case.code);
  const std::string expected_diagnostic = path + std::string{test_case.diagnostic_suffix};
  EXPECT_EQ(report.diagnostic().transform(
                [](const ConfigDiagnostic &diagnostic) { return diagnostic.message(); }),
            expected_diagnostic);
  EXPECT_EQ(report.error_count(), 1U);
}

[[nodiscard]] std::string
invalid_config_name(const testing::TestParamInfo<InvalidConfigCase> &information) {
  const std::string_view file = information.param.file;
  return std::string{file.substr(0, file.find('.'))};
}

INSTANTIATE_TEST_SUITE_P(BadConfigs, InvalidConfigTest, testing::ValuesIn(kInvalidConfigs),
                         invalid_config_name);

} // namespace
} // namespace volt::config
