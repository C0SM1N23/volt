#include "volt/core/semantic_version.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace volt::core {
namespace {

TEST(SemanticVersionTest, ExposesTheComponentsItWasConstructedWith) {
  constexpr SemanticVersion version{1, 2, 3};

  EXPECT_EQ(version.major(), 1U);
  EXPECT_EQ(version.minor(), 2U);
  EXPECT_EQ(version.patch(), 3U);
}

struct EqualityCase {
  std::string_view name;
  SemanticVersion lhs;
  SemanticVersion rhs;
  bool expected_equal;
};

class SemanticVersionEqualityTest : public ::testing::TestWithParam<EqualityCase> {};

TEST_P(SemanticVersionEqualityTest, ComparesComponentwise) {
  const auto &test_case = GetParam();
  EXPECT_EQ(test_case.lhs == test_case.rhs, test_case.expected_equal);
}

INSTANTIATE_TEST_SUITE_P(
    Cases, SemanticVersionEqualityTest,
    ::testing::Values(EqualityCase{"IdenticalVersionsAreEqual", SemanticVersion{1, 0, 0},
                                   SemanticVersion{1, 0, 0}, true},
                      EqualityCase{"DifferingPatchIsUnequal", SemanticVersion{1, 0, 0},
                                   SemanticVersion{1, 0, 1}, false},
                      EqualityCase{"DifferingMinorIsUnequal", SemanticVersion{1, 0, 0},
                                   SemanticVersion{1, 1, 0}, false},
                      EqualityCase{"DifferingMajorIsUnequal", SemanticVersion{1, 0, 0},
                                   SemanticVersion{2, 0, 0}, false}),
    [](const ::testing::TestParamInfo<EqualityCase> &param_info) {
      return std::string{param_info.param.name};
    });

} // namespace
} // namespace volt::core
