#include "volt/core/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace volt::core {
namespace {

// Three call levels where only the innermost decides the outcome. The counter
// separates "the error travelled outward" from "an outer level never ran",
// which a plain error comparison cannot tell apart.
struct PropagationTrace {
  int levels_entered = 0;
  bool reached_end_of_level_two = false;
};

expected<void> level_three(PropagationTrace &trace, expected<void> outcome) {
  trace.levels_entered += 1;
  return outcome;
}

expected<void> level_two(PropagationTrace &trace, expected<void> outcome) {
  trace.levels_entered += 1;
  VOLT_TRY(level_three(trace, outcome));
  trace.reached_end_of_level_two = true;
  return {};
}

expected<void> level_one(PropagationTrace &trace, expected<void> outcome) {
  trace.levels_entered += 1;
  VOLT_TRY(level_two(trace, outcome));
  return {};
}

TEST(VoltTryTest, CarriesTheErrorUnchangedThroughThreeLevels) {
  PropagationTrace trace;
  const expected<void> result = level_one(trace, std::unexpected{ErrorCode::kTransientTimeout});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ErrorCode::kTransientTimeout);
  EXPECT_EQ(trace.levels_entered, 3);
}

TEST(VoltTryTest, StopsTheRestOfTheFunctionAfterAFailure) {
  PropagationTrace trace;
  const expected<void> result = level_one(trace, std::unexpected{ErrorCode::kResourceBusy});

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(trace.reached_end_of_level_two);
}

TEST(VoltTryTest, LetsSuccessReachTheOutermostCaller) {
  PropagationTrace trace;
  const expected<void> result = level_one(trace, expected<void>{});

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(trace.levels_entered, 3);
  EXPECT_TRUE(trace.reached_end_of_level_two);
}

TEST(ExpectedTest, CarriesAValueOnSuccess) {
  constexpr int kPayload = 17;
  const expected<int> result = kPayload;

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, kPayload);
}

TEST(ExpectedTest, IsTheSameTypeUnderBothSpellings) {
  static_assert(std::is_same_v<volt::expected<int>, volt::core::expected<int>>);
  SUCCEED();
}

struct CategoryCase {
  std::string_view name;
  ErrorCode code;
  ErrorCategory expected_category;
};

class ErrorCategoryTest : public ::testing::TestWithParam<CategoryCase> {};

TEST_P(ErrorCategoryTest, ClassifiesTheCodeByItsLeadingDigit) {
  EXPECT_EQ(category(GetParam().code), GetParam().expected_category);
}

TEST_P(ErrorCategoryTest, DescribesTheCode) {
  EXPECT_FALSE(message(GetParam().code).empty());
  EXPECT_NE(message(GetParam().code), message(static_cast<ErrorCode>(0)));
}

INSTANTIATE_TEST_SUITE_P(
    AllCodes, ErrorCategoryTest,
    ::testing::Values(
        CategoryCase{"ConfigMissingField", ErrorCode::kConfigMissingField,
                     ErrorCategory::kConfiguration},
        CategoryCase{"ConfigInvalidValue", ErrorCode::kConfigInvalidValue,
                     ErrorCategory::kConfiguration},
        CategoryCase{"ConfigValueOutOfRange", ErrorCode::kConfigValueOutOfRange,
                     ErrorCategory::kConfiguration},
        CategoryCase{"ConfigDuplicateId", ErrorCode::kConfigDuplicateId,
                     ErrorCategory::kConfiguration},
        CategoryCase{"ConfigCyclicDependency", ErrorCode::kConfigCyclicDependency,
                     ErrorCategory::kConfiguration},
        CategoryCase{"ResourceUnavailable", ErrorCode::kResourceUnavailable,
                     ErrorCategory::kResource},
        CategoryCase{"ResourceExhausted", ErrorCode::kResourceExhausted, ErrorCategory::kResource},
        CategoryCase{"ResourceBusy", ErrorCode::kResourceBusy, ErrorCategory::kResource},
        CategoryCase{"TransientTimeout", ErrorCode::kTransientTimeout, ErrorCategory::kTransient},
        CategoryCase{"TransientMessageLost", ErrorCode::kTransientMessageLost,
                     ErrorCategory::kTransient},
        CategoryCase{"TransientIntegrityCheckFailed", ErrorCode::kTransientIntegrityCheckFailed,
                     ErrorCategory::kTransient},
        CategoryCase{"TransientPeerUnreachable", ErrorCode::kTransientPeerUnreachable,
                     ErrorCategory::kTransient},
        CategoryCase{"ExternalNotConnected", ErrorCode::kExternalNotConnected,
                     ErrorCategory::kExternal},
        CategoryCase{"ExternalRequestRejected", ErrorCode::kExternalRequestRejected,
                     ErrorCategory::kExternal},
        CategoryCase{"ExternalUnsupportedRequest", ErrorCode::kExternalUnsupportedRequest,
                     ErrorCategory::kExternal},
        CategoryCase{"InternalArithmeticOverflow", ErrorCode::kInternalArithmeticOverflow,
                     ErrorCategory::kInternal},
        CategoryCase{"InternalBufferTooSmall", ErrorCode::kInternalBufferTooSmall,
                     ErrorCategory::kInternal},
        CategoryCase{"InternalOutOfRange", ErrorCode::kInternalOutOfRange,
                     ErrorCategory::kInternal}),
    [](const ::testing::TestParamInfo<CategoryCase> &param_info) {
      return std::string{param_info.param.name};
    });

TEST(ErrorCodeTest, ReportsUnrecognisedValuesInsteadOfClaimingAKnownOne) {
  constexpr auto kUnassigned = static_cast<ErrorCode>(0x1FFF);
  EXPECT_EQ(message(kUnassigned), "unrecognised error code");
}

TEST(ErrorCodeTest, ClassifiesCodesWithoutALookupTableAtCompileTime) {
  static_assert(category(ErrorCode::kConfigMissingField) == ErrorCategory::kConfiguration);
  static_assert(category(ErrorCode::kInternalBufferTooSmall) == ErrorCategory::kInternal);
  SUCCEED();
}

} // namespace
} // namespace volt::core
