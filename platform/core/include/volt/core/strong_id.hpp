#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace volt::core {

/// Identifier that carries its own tag, so two kinds of identifier neither
/// convert into one another nor swap places at a call site.
///
/// `Tag` is only ever named, never defined: it exists to keep instantiations
/// distinct. `Repr` is the value that goes on the wire.
template <typename Tag, typename Repr = std::uint32_t> class StrongId final {
public:
  using underlying_type = Repr;

  /// Constructs the zero identifier.
  constexpr StrongId() noexcept = default;

  /// Wraps an underlying value. Explicit, so a bare integer never becomes an
  /// identifier by accident.
  constexpr explicit StrongId(Repr value) noexcept : value_{value} {}

  /// Returns the underlying value, for serialisation and hashing.
  [[nodiscard]] constexpr Repr value() const noexcept { return value_; }

  /// Orders and compares identifiers of the same kind. Comparing different
  /// kinds does not compile, which is the point of the type.
  [[nodiscard]] constexpr auto operator<=>(const StrongId &) const noexcept = default;
  [[nodiscard]] constexpr bool operator==(const StrongId &) const noexcept = default;

private:
  Repr value_{};
};

} // namespace volt::core

/// Lets identifiers key the standard unordered containers.
// NOLINTNEXTLINE(cert-dcl58-cpp) — deviation: DEV-001
template <typename Tag, typename Repr> struct std::hash<volt::core::StrongId<Tag, Repr>> {
  [[nodiscard]] std::size_t operator()(volt::core::StrongId<Tag, Repr> identifier) const noexcept {
    return std::hash<Repr>{}(identifier.value());
  }
};
