#include "volt/sched/hdr_histogram.hpp"

#include <algorithm>

namespace volt::sched {

std::uint64_t HdrHistogram::percentile(double quantile) const noexcept {
  const std::uint64_t recorded = total();
  if (recorded == 0) {
    return 0;
  }
  const double clamped = std::clamp(quantile, 0.0, 1.0);
  // The rank of the requested quantile, at least one so P0 is the smallest
  // recorded value's bucket rather than nothing.
  std::uint64_t rank = static_cast<std::uint64_t>(clamped * static_cast<double>(recorded));
  if (rank == 0) {
    rank = 1;
  }

  std::uint64_t seen = 0;
  for (std::size_t index = 0; index < kBucketCount; ++index) {
    seen += buckets_[index].load(std::memory_order_relaxed);
    if (seen >= rank) {
      // The exact maximum is cheaper to keep than a wider last bucket, and
      // a tail percentile clipped to it reads better than a bucket bound
      // beyond anything that happened.
      const std::uint64_t bound = upper_bound_of(index);
      const std::uint64_t ceiling = max();
      return bound < ceiling ? bound : ceiling;
    }
  }
  return max();
}

} // namespace volt::sched
