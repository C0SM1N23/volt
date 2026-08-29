#pragma once

#include <cstdint>
#include <string_view>

namespace volt::pal::sim::detail {

/// A running digest of everything that happened in a simulated world.
///
/// Two runs of the same scenario under the same seed have to produce the same
/// digest; if they do not, the world is not deterministic and every result
/// obtained from it is unrepeatable. Keeping a digest rather than a list of
/// events means the check costs eight bytes and stays valid for a run of any
/// length.
///
/// Order matters by construction: each event folds into the digest that
/// preceded it, so two runs that produce the same events in a different order
/// disagree.
class SimEventLog final {
public:
  /// Folds one event into the digest.
  ///
  /// @pre `name` names a kind of event, not one occurrence of it; the value
  ///      carries what distinguishes occurrences.
  void record(std::string_view name, std::uint64_t value) noexcept;

  /// Returns the digest of every event recorded so far.
  [[nodiscard]] std::uint64_t digest() const noexcept { return digest_; }

private:
  std::uint64_t digest_ = 0;
};

} // namespace volt::pal::sim::detail
