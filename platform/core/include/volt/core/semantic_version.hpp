#pragma once

#include <cstdint>

namespace volt::core {

/// Semantic version number identifying a VOLT build artifact.
class SemanticVersion final {
public:
    /// Constructs a semantic version from its three components.
    constexpr SemanticVersion(std::uint32_t major_version,
                               std::uint32_t minor_version,
                               std::uint32_t patch_version) noexcept
        : major_{major_version}, minor_{minor_version}, patch_{patch_version} {}

    /// Returns the major version component.
    [[nodiscard]] constexpr std::uint32_t major() const noexcept { return major_; }

    /// Returns the minor version component.
    [[nodiscard]] constexpr std::uint32_t minor() const noexcept { return minor_; }

    /// Returns the patch version component.
    [[nodiscard]] constexpr std::uint32_t patch() const noexcept { return patch_; }

    [[nodiscard]] constexpr bool operator==(const SemanticVersion& other) const noexcept = default;

private:
    std::uint32_t major_;
    std::uint32_t minor_;
    std::uint32_t patch_;
};

}  // namespace volt::core
