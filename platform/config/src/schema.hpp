#pragma once

#include "volt/config/load_report.hpp"

#include "volt/core/error.hpp"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace volt::config::detail {

enum class ValueKind : std::uint8_t {
  kMap,
  kSequence,
  kString,
  kBoolean,
  kInteger,
  kEnum,
  kEnumOrMap,
};

struct IntegerConstraint final {
  std::int64_t minimum;
  std::int64_t maximum;
  std::string_view unit;
};

struct SequenceConstraint final {
  std::size_t minimum_items;
  std::size_t maximum_items;
};

struct SchemaNode;

struct FieldRule final {
  std::string_view name;
  const SchemaNode *schema;
  bool required;
  bool calibratable;
};

enum class DependencyKind : std::uint8_t { kRequiresValue, kLessThan };

struct DependencyRule final {
  DependencyKind kind;
  std::string_view trigger_field;
  std::string_view related_field;
  std::string_view required_value;
};

struct SchemaNode final {
  ValueKind kind;
  IntegerConstraint integer;
  SequenceConstraint sequence;
  std::span<const std::string_view> enum_values;
  std::span<const FieldRule> fields;
  std::span<const DependencyRule> dependencies;
  const SchemaNode *item;
  std::string_view expectation;
};

/// Returns the strict schema for a complete node document.
[[nodiscard]] const SchemaNode &node_schema() noexcept;

/// Returns the strict schema for a standalone cluster document.
[[nodiscard]] const SchemaNode &cluster_schema() noexcept;

/// Parses and validates a YAML file while recording every consumed field.
[[nodiscard]] volt::expected<YAML::Node>
load_validated_yaml(std::string_view path, const SchemaNode &schema, LoadReport &report);

/// Reports whether a complete node field is hot-reloadable.
[[nodiscard]] bool is_calibratable(std::string_view path) noexcept;

} // namespace volt::config::detail
