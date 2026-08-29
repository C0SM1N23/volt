#include "schema.hpp"

#include "report_writer.hpp"

#include "volt/core/error_code.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace volt::config::detail {
namespace {

constexpr SchemaNode make_scalar(ValueKind kind, std::string_view expectation) noexcept {
  return SchemaNode{kind, {}, {}, {}, {}, {}, nullptr, expectation};
}

constexpr SchemaNode make_integer(std::int64_t minimum, std::int64_t maximum,
                                  std::string_view unit) noexcept {
  return SchemaNode{ValueKind::kInteger, {minimum, maximum, unit}, {}, {}, {}, {}, nullptr, {}};
}

constexpr SchemaNode make_enum(std::span<const std::string_view> values) noexcept {
  return SchemaNode{ValueKind::kEnum, {}, {}, values, {}, {}, nullptr, {}};
}

constexpr SchemaNode make_map(std::span<const FieldRule> fields,
                              std::span<const DependencyRule> dependencies = {}) noexcept {
  return SchemaNode{ValueKind::kMap, {}, {}, {}, fields, dependencies, nullptr, "mapping"};
}

constexpr SchemaNode make_sequence(const SchemaNode &item, std::size_t minimum_items,
                                   std::size_t maximum_items) noexcept {
  return SchemaNode{
      ValueKind::kSequence, {}, {minimum_items, maximum_items}, {}, {}, {}, &item, "sequence"};
}

constexpr SchemaNode make_enum_or_map(std::span<const std::string_view> values,
                                      std::span<const FieldRule> fields) noexcept {
  return SchemaNode{ValueKind::kEnumOrMap,   {}, {}, values, fields, {}, nullptr,
                    "enum scalar or mapping"};
}

constexpr SchemaNode kStringSchema = make_scalar(ValueKind::kString, "string");
constexpr SchemaNode kBooleanSchema = make_scalar(ValueKind::kBoolean, "boolean (true or false)");

// Positive timing values use signed 32-bit source units. This ceiling follows the manual YAML
// representation used by Annex B; raising it would require rechecking Duration conversion bounds.
constexpr std::int64_t kMinimumPositiveValue = 1;
constexpr std::int64_t kMaximumTimingValue = std::numeric_limits<std::int32_t>::max();
constexpr SchemaNode kMillisecondsSchema =
    make_integer(kMinimumPositiveValue, kMaximumTimingValue, "ms");
constexpr SchemaNode kMicrosecondsSchema =
    make_integer(kMinimumPositiveValue, kMaximumTimingValue, "us");

// Linux cpu_set_t exposes 1024 CPU positions. Raising this without changing the PAL affinity
// representation would admit a CPU that cannot be selected.
constexpr std::int64_t kMinimumCpuId = 0;
constexpr std::int64_t kMaximumCpuId = 1023;
constexpr SchemaNode kCpuIdSchema = make_integer(kMinimumCpuId, kMaximumCpuId, "CPU index");

// POSIX SCHED_FIFO priorities occupy the inclusive 1..99 range on the supported Linux target.
constexpr std::int64_t kMinimumRealtimePriority = 1;
constexpr std::int64_t kMaximumRealtimePriority = 99;
constexpr SchemaNode kPrioritySchema =
    make_integer(kMinimumRealtimePriority, kMaximumRealtimePriority, "priority");

// TCP and UDP port zero is reserved; 65535 is the largest value carried by the 16-bit field.
constexpr std::int64_t kMinimumNetworkPort = 1;
constexpr std::int64_t kMaximumNetworkPort = std::numeric_limits<std::uint16_t>::max();
constexpr SchemaNode kPortSchema = make_integer(kMinimumNetworkPort, kMaximumNetworkPort, "port");

// CAN-FD retains the CAN 2.0 29-bit extended identifier space. A larger value cannot be encoded.
constexpr std::int64_t kMinimumCanId = 0;
constexpr std::int64_t kMaximumCanId = 0x1FFFFFFF;
constexpr SchemaNode kCanIdSchema = make_integer(kMinimumCanId, kMaximumCanId, "CAN identifier");

// Probe counts and miss thresholds are bounded by their uint16_t typed representation.
constexpr std::int64_t kMinimumCount = 1;
constexpr std::int64_t kMaximumCount = std::numeric_limits<std::uint16_t>::max();
constexpr SchemaNode kCountSchema = make_integer(kMinimumCount, kMaximumCount, "count");

constexpr std::array<std::string_view, 2> kNodeRoles{"COMPUTE", "SAFETY"};
constexpr std::array<std::string_view, 3> kPtpRoles{"AUTO", "MASTER", "FOLLOWER"};
constexpr std::array<std::string_view, 3> kIsolationValues{"THREAD", "PROCESS", "PARTITION"};
constexpr std::array<std::string_view, 4> kCriticalityValues{"BEST_EFFORT", "MEDIUM", "HIGH",
                                                             "SAFETY_CRITICAL"};
constexpr std::array<std::string_view, 1> kSchedulerPolicies{"SCHED_FIFO"};
constexpr std::array<std::string_view, 2> kLeaseClasses{"FENCED", "UNFENCED"};
constexpr std::array<std::string_view, 2> kReplicationModes{"NONE", "ACTIVE_STANDBY"};

constexpr SchemaNode kNodeRoleSchema = make_enum(kNodeRoles);
constexpr SchemaNode kPtpRoleSchema = make_enum(kPtpRoles);
constexpr SchemaNode kIsolationSchema = make_enum(kIsolationValues);
constexpr SchemaNode kCriticalitySchema = make_enum(kCriticalityValues);
constexpr SchemaNode kSchedulerPolicySchema = make_enum(kSchedulerPolicies);
constexpr SchemaNode kLeaseClassSchema = make_enum(kLeaseClasses);
constexpr SchemaNode kReplicationModeSchema = make_enum(kReplicationModes);

// Annex B targets 2-4 nodes, so a local node has one to three peers.
constexpr std::size_t kMinimumPeers = 1;
constexpr std::size_t kMaximumPeers = 3;
constexpr SchemaNode kPeerSequenceSchema =
    make_sequence(kStringSchema, kMinimumPeers, kMaximumPeers);

// A node must carry at least one role and Annex B defines exactly the two independent roles.
constexpr std::size_t kMinimumRoles = 1;
constexpr std::size_t kMaximumRoles = kNodeRoles.size();
constexpr SchemaNode kRoleSequenceSchema =
    make_sequence(kNodeRoleSchema, kMinimumRoles, kMaximumRoles);

// Affinity lists cannot exceed the same cpu_set_t capacity that bounds each identifier.
constexpr std::size_t kMinimumCpuCount = 1;
constexpr std::size_t kMaximumCpuCount = static_cast<std::size_t>(kMaximumCpuId) + 1U;
constexpr SchemaNode kCpuSequenceSchema =
    make_sequence(kCpuIdSchema, kMinimumCpuCount, kMaximumCpuCount);

// Raft specifies the inclusive lower and upper election bounds as exactly two entries.
constexpr std::size_t kElectionBoundCount = 2;
constexpr SchemaNode kElectionTimeoutSchema =
    make_sequence(kMillisecondsSchema, kElectionBoundCount, kElectionBoundCount);

constexpr std::array<FieldRule, 3> kInterfaceFields{{
    {"can", &kStringSchema, true, false},
    {"eth", &kStringSchema, true, false},
    {"diag_eth", &kStringSchema, true, false},
}};
constexpr SchemaNode kInterfacesSchema = make_map(kInterfaceFields);

constexpr std::array<FieldRule, 3> kRealtimeFields{{
    {"isolated_cpus", &kCpuSequenceSchema, true, false},
    {"mlockall", &kBooleanSchema, true, false},
    {"sched_reset_on_fork", &kBooleanSchema, true, false},
}};
constexpr SchemaNode kRealtimeSchema = make_map(kRealtimeFields);

constexpr std::array<FieldRule, 4> kNodeFields{{
    {"id", &kStringSchema, true, false},
    {"role", &kRoleSequenceSchema, true, false},
    {"interfaces", &kInterfacesSchema, true, false},
    {"rt", &kRealtimeSchema, true, false},
}};
constexpr SchemaNode kNodeIdentitySchema = make_map(kNodeFields);

constexpr std::array<FieldRule, 2> kRaftFields{{
    {"election_timeout_ms", &kElectionTimeoutSchema, true, false},
    {"heartbeat_ms", &kMillisecondsSchema, true, false},
}};
constexpr SchemaNode kRaftSchema = make_map(kRaftFields);

constexpr std::array<FieldRule, 5> kSwimFields{{
    {"period_ms", &kMillisecondsSchema, true, false},
    {"timeout_ms", &kMillisecondsSchema, true, false},
    {"indirect_k", &kCountSchema, true, false},
    {"indirect_ms", &kMillisecondsSchema, true, false},
    {"suspect_ms", &kMillisecondsSchema, true, false},
}};
constexpr std::array<DependencyRule, 1> kSwimDependencies{{
    {DependencyKind::kLessThan, "timeout_ms", "period_ms", {}},
}};
constexpr SchemaNode kSwimSchema = make_map(kSwimFields, kSwimDependencies);

constexpr std::array<FieldRule, 3> kCanHeartbeatFields{{
    {"id", &kCanIdSchema, true, false},
    {"period_ms", &kMillisecondsSchema, true, false},
    {"miss_threshold", &kCountSchema, true, false},
}};
constexpr SchemaNode kCanHeartbeatSchema = make_map(kCanHeartbeatFields);

constexpr std::array<FieldRule, 3> kPtpFields{{
    {"role", &kPtpRoleSchema, true, false},
    {"sync_interval_ms", &kMillisecondsSchema, true, false},
    {"max_offset_us", &kMicrosecondsSchema, true, false},
}};
constexpr SchemaNode kPtpSchema = make_map(kPtpFields);

constexpr std::array<FieldRule, 5> kClusterFields{{
    {"peers", &kPeerSequenceSchema, true, false},
    {"raft", &kRaftSchema, true, false},
    {"swim", &kSwimSchema, true, false},
    {"can_heartbeat", &kCanHeartbeatSchema, true, false},
    {"ptp", &kPtpSchema, true, false},
}};
constexpr SchemaNode kClusterSchema = make_map(kClusterFields);

constexpr std::array<FieldRule, 2> kSchedulerFields{{
    {"policy", &kSchedulerPolicySchema, true, false},
    {"priority", &kPrioritySchema, true, false},
}};
constexpr SchemaNode kSchedulerSchema = make_map(kSchedulerFields);

constexpr std::array<FieldRule, 6> kLeaseFields{{
    {"resource", &kStringSchema, true, false},
    {"class", &kLeaseClassSchema, true, false},
    {"duration_ms", &kMillisecondsSchema, true, false},
    {"renew_ms", &kMillisecondsSchema, true, false},
    {"self_yield_ms", &kMillisecondsSchema, true, false},
    {"preauthorized_successor", &kStringSchema, true, false},
}};
constexpr std::array<DependencyRule, 2> kLeaseDependencies{{
    {DependencyKind::kLessThan, "renew_ms", "duration_ms", {}},
    {DependencyKind::kLessThan, "self_yield_ms", "duration_ms", {}},
}};
constexpr SchemaNode kLeaseSchema = make_map(kLeaseFields, kLeaseDependencies);

constexpr SchemaNode kStandbySequenceSchema =
    make_sequence(kStringSchema, kMinimumPeers, kMaximumPeers);
constexpr std::array<FieldRule, 3> kReplicationFields{{
    {"mode", &kReplicationModeSchema, true, false},
    {"sync_period_us", &kMicrosecondsSchema, true, false},
    {"standby_on", &kStandbySequenceSchema, true, false},
}};
constexpr SchemaNode kReplicationSchema = make_enum_or_map(kReplicationModes, kReplicationFields);

constexpr std::array<DependencyRule, 2> kServiceDependencies{{
    {DependencyKind::kRequiresValue, "sched", "isolation", "PARTITION"},
    {DependencyKind::kRequiresValue, "lease", "criticality", "SAFETY_CRITICAL"},
}};
constexpr std::array<FieldRule, 8> kServiceFields{{
    {"name", &kStringSchema, true, false},
    {"isolation", &kIsolationSchema, true, false},
    {"criticality", &kCriticalitySchema, true, false},
    {"period_us", &kMicrosecondsSchema, true, false},
    {"cpu", &kCpuSequenceSchema, true, false},
    {"sched", &kSchedulerSchema, false, false},
    {"lease", &kLeaseSchema, false, false},
    {"replication", &kReplicationSchema, false, false},
}};
constexpr SchemaNode kServiceSchema = make_map(kServiceFields, kServiceDependencies);

// A node configuration is statically bounded to 64 services so malformed input cannot grow
// initialization memory without limit. Increasing it raises startup validation and storage cost.
constexpr std::size_t kMinimumServices = 1;
constexpr std::size_t kMaximumServices = 64;
constexpr SchemaNode kServiceSequenceSchema =
    make_sequence(kServiceSchema, kMinimumServices, kMaximumServices);

constexpr std::array<FieldRule, 3> kTelemetryFields{{
    {"trace", &kBooleanSchema, true, true},
    {"metrics_port", &kPortSchema, true, false},
    {"record", &kBooleanSchema, true, true},
}};
constexpr SchemaNode kTelemetrySchema = make_map(kTelemetryFields);

constexpr std::array<FieldRule, 4> kRootFields{{
    {"node", &kNodeIdentitySchema, true, false},
    {"cluster", &kClusterSchema, true, false},
    {"services", &kServiceSequenceSchema, true, false},
    {"telemetry", &kTelemetrySchema, true, false},
}};
constexpr SchemaNode kNodeSchema = make_map(kRootFields);

[[nodiscard]] std::size_t source_line(const YAML::Node &node) {
  const YAML::Mark mark = node.Mark();
  return mark.is_null() ? 0U : static_cast<std::size_t>(mark.line) + 1U;
}

[[nodiscard]] bool parse_integer(std::string_view text, std::int64_t &value) noexcept {
  const bool hexadecimal = text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  const std::string_view digits = hexadecimal ? text.substr(2U) : text;
  const int base = hexadecimal ? 16 : 10;
  const std::from_chars_result result =
      std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
  return !digits.empty() && result.ec == std::errc{} && result.ptr == digits.data() + digits.size();
}

[[nodiscard]] bool is_plain_string(const YAML::Node &node) {
  if (!node.IsScalar()) {
    return false;
  }
  if (node.Tag() == "!") {
    return true;
  }
  const std::string &scalar = node.Scalar();
  std::int64_t ignored = 0;
  return scalar != "true" && scalar != "false" && scalar != "null" && scalar != "~" &&
         !parse_integer(scalar, ignored);
}

[[nodiscard]] std::string rendered_value(const YAML::Node &node) {
  if (!node || node.IsNull()) {
    return "<null>";
  }
  if (node.IsMap()) {
    return "<mapping>";
  }
  if (node.IsSequence()) {
    return "<sequence>";
  }
  return "'" + node.Scalar() + "'";
}

[[nodiscard]] std::string enum_expectation(std::span<const std::string_view> values) {
  std::string result = "one of [";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      result += ", ";
    }
    result += values[index];
  }
  result += "]";
  return result;
}

[[nodiscard]] std::string field_expectation(std::span<const FieldRule> fields) {
  std::string result = "known field: one of [";
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0U) {
      result += ", ";
    }
    result += fields[index].name;
  }
  result += "]";
  return result;
}

[[nodiscard]] std::string integer_expectation(const IntegerConstraint &constraint) {
  return "integer in range [" + std::to_string(constraint.minimum) + ", " +
         std::to_string(constraint.maximum) + "] " + std::string{constraint.unit};
}

[[nodiscard]] std::string sequence_expectation(const SchemaNode &schema) {
  return "sequence with " + std::to_string(schema.sequence.minimum_items) + ".." +
         std::to_string(schema.sequence.maximum_items) + " items";
}

struct FailureDetail final {
  std::string field;
  std::string found;
  std::string expected;
  volt::core::ErrorCode code;
};

[[nodiscard]] volt::core::ErrorCode report_error(ReportWriter &writer, std::string_view file,
                                                 const YAML::Node &node, FailureDetail failure) {
  return writer.fail(ConfigDiagnostic{failure.code, std::string{file}, source_line(node),
                                      std::move(failure.field), std::move(failure.found),
                                      std::move(failure.expected)});
}

[[nodiscard]] const FieldRule *find_field(const SchemaNode &schema,
                                          std::string_view name) noexcept {
  const auto position = std::ranges::find_if(
      schema.fields, [name](const FieldRule &field) { return field.name == name; });
  return position == schema.fields.end() ? nullptr : &*position;
}

[[nodiscard]] bool contains_enum(std::span<const std::string_view> values,
                                 std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::string child_path(std::string_view parent, std::string_view child) {
  return parent.empty() ? std::string{child} : std::string{parent} + "." + std::string{child};
}

[[nodiscard]] volt::expected<void> validate_node(const YAML::Node &node, const SchemaNode &schema,
                                                 std::string_view path, std::string_view file,
                                                 ReportWriter &writer);

[[nodiscard]] volt::expected<void> validate_integer(const YAML::Node &node,
                                                    const SchemaNode &schema, std::string_view path,
                                                    std::string_view file, ReportWriter &writer) {
  std::int64_t value = 0;
  if (!node.IsScalar() || !parse_integer(node.Scalar(), value)) {
    const auto code =
        report_error(writer, file, node,
                     {std::string{path}, rendered_value(node), integer_expectation(schema.integer),
                      volt::core::ErrorCode::kConfigInvalidValue});
    return std::unexpected{code};
  }
  if (value < schema.integer.minimum || value > schema.integer.maximum) {
    const auto code =
        report_error(writer, file, node,
                     {std::string{path}, rendered_value(node), integer_expectation(schema.integer),
                      volt::core::ErrorCode::kConfigValueOutOfRange});
    return std::unexpected{code};
  }
  return {};
}

[[nodiscard]] volt::expected<void> validate_enum(const YAML::Node &node,
                                                 std::span<const std::string_view> values,
                                                 std::string_view path, std::string_view file,
                                                 ReportWriter &writer) {
  if (node.IsScalar() && contains_enum(values, node.Scalar())) {
    return {};
  }
  const auto code = report_error(writer, file, node,
                                 {std::string{path}, rendered_value(node), enum_expectation(values),
                                  volt::core::ErrorCode::kConfigInvalidValue});
  return std::unexpected{code};
}

[[nodiscard]] volt::expected<void> validate_sequence(const YAML::Node &node,
                                                     const SchemaNode &schema,
                                                     std::string_view path, std::string_view file,
                                                     ReportWriter &writer) {
  if (!node.IsSequence() || node.size() < schema.sequence.minimum_items ||
      node.size() > schema.sequence.maximum_items) {
    const auto code =
        report_error(writer, file, node,
                     {std::string{path}, rendered_value(node), sequence_expectation(schema),
                      volt::core::ErrorCode::kConfigInvalidValue});
    return std::unexpected{code};
  }
  for (std::size_t index = 0; index < node.size(); ++index) {
    const std::string item_path = std::string{path} + "[" + std::to_string(index) + "]";
    const auto result = validate_node(node[index], *schema.item, item_path, file, writer);
    if (!result.has_value()) {
      return result;
    }
  }
  return {};
}

[[nodiscard]] volt::expected<void>
validate_dependencies(const YAML::Node &node, const SchemaNode &schema, std::string_view path,
                      std::string_view file, ReportWriter &writer) {
  for (const DependencyRule &dependency : schema.dependencies) {
    const YAML::Node trigger = node[std::string{dependency.trigger_field}];
    const YAML::Node related = node[std::string{dependency.related_field}];
    if (!trigger || !related) {
      continue;
    }
    const bool satisfied = dependency.kind == DependencyKind::kRequiresValue
                               ? related.IsScalar() && related.Scalar() == dependency.required_value
                               : trigger.as<std::int64_t>() < related.as<std::int64_t>();
    if (satisfied) {
      continue;
    }
    const std::string expected =
        dependency.kind == DependencyKind::kRequiresValue
            ? "field '" + std::string{dependency.related_field} + "' equal to '" +
                  std::string{dependency.required_value} + "'"
            : "value less than field '" + std::string{dependency.related_field} + "'";
    const auto code =
        report_error(writer, file, trigger,
                     {child_path(path, dependency.trigger_field), rendered_value(trigger), expected,
                      volt::core::ErrorCode::kConfigInvalidValue});
    return std::unexpected{code};
  }
  return {};
}

[[nodiscard]] volt::expected<void> validate_map_fields(const YAML::Node &node,
                                                       const SchemaNode &schema,
                                                       std::string_view path, std::string_view file,
                                                       ReportWriter &writer) {
  std::set<std::string, std::less<>> seen;
  for (const auto &entry : node) {
    if (!entry.first.IsScalar()) {
      const auto code = report_error(writer, file, entry.first,
                                     {std::string{path}, "non-scalar key", "string field name",
                                      volt::core::ErrorCode::kConfigInvalidValue});
      return std::unexpected{code};
    }
    const std::string name = entry.first.Scalar();
    const std::string full_path = child_path(path, name);
    if (!seen.insert(name).second) {
      const auto code = report_error(writer, file, entry.first,
                                     {full_path, "duplicate field", "field declared once",
                                      volt::core::ErrorCode::kConfigDuplicateId});
      return std::unexpected{code};
    }
    const FieldRule *field = find_field(schema, name);
    if (field == nullptr) {
      const auto code =
          report_error(writer, file, entry.first,
                       {full_path, "unknown field '" + name + "'", field_expectation(schema.fields),
                        volt::core::ErrorCode::kConfigInvalidValue});
      return std::unexpected{code};
    }
    writer.consume(full_path, source_line(entry.first));
    const auto result = validate_node(entry.second, *field->schema, full_path, file, writer);
    if (!result.has_value()) {
      return result;
    }
  }
  return {};
}

[[nodiscard]] volt::expected<void>
validate_required_fields(const YAML::Node &node, const SchemaNode &schema, std::string_view path,
                         std::string_view file, ReportWriter &writer) {
  for (const FieldRule &field : schema.fields) {
    if (!field.required || node[std::string{field.name}]) {
      continue;
    }
    const auto code = report_error(writer, file, node,
                                   {child_path(path, field.name), "<missing>", "required field",
                                    volt::core::ErrorCode::kConfigMissingField});
    return std::unexpected{code};
  }
  return {};
}

[[nodiscard]] volt::expected<void> validate_map(const YAML::Node &node, const SchemaNode &schema,
                                                std::string_view path, std::string_view file,
                                                ReportWriter &writer) {
  if (!node.IsMap()) {
    const auto code =
        report_error(writer, file, node,
                     {std::string{path}, rendered_value(node), std::string{schema.expectation},
                      volt::core::ErrorCode::kConfigInvalidValue});
    return std::unexpected{code};
  }
  const auto fields_result = validate_map_fields(node, schema, path, file, writer);
  if (!fields_result.has_value()) {
    return fields_result;
  }
  const auto required_result = validate_required_fields(node, schema, path, file, writer);
  if (!required_result.has_value()) {
    return required_result;
  }
  return validate_dependencies(node, schema, path, file, writer);
}

[[nodiscard]] volt::expected<void> validate_node(const YAML::Node &node, const SchemaNode &schema,
                                                 std::string_view path, std::string_view file,
                                                 ReportWriter &writer) {
  switch (schema.kind) {
  case ValueKind::kMap:
    return validate_map(node, schema, path, file, writer);
  case ValueKind::kSequence:
    return validate_sequence(node, schema, path, file, writer);
  case ValueKind::kString:
    if (is_plain_string(node)) {
      return {};
    }
    break;
  case ValueKind::kBoolean:
    if (node.IsScalar() && (node.Scalar() == "true" || node.Scalar() == "false")) {
      return {};
    }
    break;
  case ValueKind::kInteger:
    return validate_integer(node, schema, path, file, writer);
  case ValueKind::kEnum:
    return validate_enum(node, schema.enum_values, path, file, writer);
  case ValueKind::kEnumOrMap:
    return node.IsMap() ? validate_map(node, schema, path, file, writer)
                        : validate_enum(node, schema.enum_values, path, file, writer);
  }
  const auto code =
      report_error(writer, file, node,
                   {std::string{path}, rendered_value(node), std::string{schema.expectation},
                    volt::core::ErrorCode::kConfigInvalidValue});
  return std::unexpected{code};
}

} // namespace

const SchemaNode &node_schema() noexcept { return kNodeSchema; }

const SchemaNode &cluster_schema() noexcept { return kClusterSchema; }

volt::expected<YAML::Node> load_validated_yaml(std::string_view path, const SchemaNode &schema,
                                               LoadReport &report) {
  ReportWriter writer{report};
  writer.reset();
  const std::string file{path};
  try {
    YAML::Node root = YAML::LoadFile(file);
    const auto validation = validate_node(root, schema, {}, file, writer);
    if (!validation.has_value()) {
      return std::unexpected{validation.error()};
    }
    writer.finish();
    return root;
  } catch (const YAML::Exception &exception) {
    const std::size_t line =
        exception.mark.is_null() ? 0U : static_cast<std::size_t>(exception.mark.line) + 1U;
    const auto code = writer.fail(
        ConfigDiagnostic{volt::core::ErrorCode::kConfigInvalidValue, file, line, "<document>",
                         "YAML parse failure '" + std::string{exception.what()} + "'",
                         "valid readable YAML document"});
    return std::unexpected{code};
  }
}

bool is_calibratable(std::string_view path) noexcept {
  constexpr std::string_view kTelemetryPrefix = "telemetry.";
  if (!path.starts_with(kTelemetryPrefix)) {
    return false;
  }
  const FieldRule *field = find_field(kTelemetrySchema, path.substr(kTelemetryPrefix.size()));
  return field != nullptr && field->calibratable;
}

} // namespace volt::config::detail
