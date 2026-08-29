#pragma once

#include "volt/core/error.hpp"

#include <cstddef>
#include <span>

namespace volt::log {

/// Where drained records end up.
///
/// An interface so a test can collect records in memory and a deployment can
/// write them to disk without the drain knowing which it is talking to.
class ILogSink {
public:
  ILogSink() = default;
  virtual ~ILogSink() = default;

  ILogSink(const ILogSink &) = delete;
  ILogSink &operator=(const ILogSink &) = delete;
  ILogSink(ILogSink &&) = delete;
  ILogSink &operator=(ILogSink &&) = delete;

  /// Writes one complete record.
  ///
  /// @pre    `record` only has to stay alive for the call
  /// @errors kResourceExhausted when there is no room left for it
  [[nodiscard]] virtual core::expected<void> write(std::span<const std::byte> record) noexcept = 0;

  /// Pushes anything buffered onward.
  [[nodiscard]] virtual core::expected<void> flush() noexcept = 0;
};

} // namespace volt::log
