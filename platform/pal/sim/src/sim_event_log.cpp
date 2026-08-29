#include "sim_event_log.hpp"

#include "volt/core/error.hpp"
#include "volt/core/hash.hpp"
#include "volt/core/span_utils.hpp"

#include <array>
#include <cstddef>

namespace volt::pal::sim::detail {

void SimEventLog::record(std::string_view name, std::uint64_t value) noexcept {
  constexpr std::size_t kWord = sizeof(std::uint64_t);
  std::array<std::byte, kWord * 2> entry{};

  // The buffer is sized from the two fields it holds, so neither write can
  // fail; a failure would mean the size computation above is wrong.
  const core::expected<void> name_written =
      core::write_little_endian<std::uint64_t>(entry, 0, core::xxhash64(name));
  VOLT_ASSERT(name_written.has_value(), "event name did not fit its own buffer");
  const core::expected<void> value_written =
      core::write_little_endian<std::uint64_t>(entry, kWord, value);
  VOLT_ASSERT(value_written.has_value(), "event value did not fit its own buffer");

  // Seeding with the previous digest is what makes the result depend on the
  // order events arrived in, not just on the set of them.
  digest_ = core::xxhash64(entry, digest_);
}

} // namespace volt::pal::sim::detail
