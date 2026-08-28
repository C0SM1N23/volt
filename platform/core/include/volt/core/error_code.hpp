#pragma once

#include <cstdint>
#include <string_view>

namespace volt::core {

/// Class an error belongs to, which decides how a caller must react to it.
/// The classes are the taxonomy of SPEC 42.1.
enum class ErrorCategory : std::uint8_t {
  kConfiguration = 0x1,
  kResource = 0x2,
  kTransient = 0x3,
  kExternal = 0x4,
  kInternal = 0x5,
};

/// Every failure VOLT reports through `expected`.
///
/// The leading hex digit is the ErrorCategory, so a code classifies itself
/// without a lookup table. Codes are append-only once assigned: they travel
/// into logs, traces and diagnostic records, where a reused number would make
/// old captures lie.
enum class ErrorCode : std::uint16_t {
  // Configuration. Rejected during validation, before the system starts.
  kConfigMissingField = 0x1001,
  kConfigInvalidValue = 0x1002,
  kConfigValueOutOfRange = 0x1003,
  kConfigDuplicateId = 0x1004,
  kConfigCyclicDependency = 0x1005,

  // Resource. Something the system needs is not available.
  kResourceUnavailable = 0x2001,
  kResourceExhausted = 0x2002,
  kResourceBusy = 0x2003,

  // Transient. Expected during normal operation; counted and debounced rather
  // than treated as a fault on first occurrence.
  kTransientTimeout = 0x3001,
  kTransientMessageLost = 0x3002,
  kTransientIntegrityCheckFailed = 0x3003,
  kTransientPeerUnreachable = 0x3004,

  // External. A peer behaved in a way the protocol already accounts for.
  kExternalNotConnected = 0x4001,
  kExternalRequestRejected = 0x4002,
  kExternalUnsupportedRequest = 0x4003,

  // Internal. The caller asked for something the operation cannot represent.
  kInternalArithmeticOverflow = 0x5001,
  kInternalBufferTooSmall = 0x5002,
  kInternalOutOfRange = 0x5003,
};

/// Returns the taxonomy class the code belongs to.
[[nodiscard]] constexpr ErrorCategory category(ErrorCode code) noexcept {
  // Bit position of the category digit within the 16-bit code, as laid out in
  // the ErrorCode comment above. Moving it would reclassify every code.
  constexpr unsigned kCategoryShift = 12;
  return static_cast<ErrorCategory>(static_cast<std::uint16_t>(code) >> kCategoryShift);
}

/// Returns what went wrong and what the caller is expected to do about it.
[[nodiscard]] constexpr std::string_view message(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::kConfigMissingField:
    return "required configuration field is absent; add it before starting";
  case ErrorCode::kConfigInvalidValue:
    return "configuration value has the wrong shape or type; correct the file";
  case ErrorCode::kConfigValueOutOfRange:
    return "configuration value is outside its declared range; correct the file";
  case ErrorCode::kConfigDuplicateId:
    return "identifier is declared twice; make it unique";
  case ErrorCode::kConfigCyclicDependency:
    return "declared dependencies form a cycle; break it";
  case ErrorCode::kResourceUnavailable:
    return "resource is not present; check that it was provisioned";
  case ErrorCode::kResourceExhausted:
    return "preallocated capacity is used up; raise the configured budget";
  case ErrorCode::kResourceBusy:
    return "resource is held elsewhere; retry with backoff or release it";
  case ErrorCode::kTransientTimeout:
    return "operation did not complete within its deadline; retry or degrade";
  case ErrorCode::kTransientMessageLost:
    return "message did not arrive; count it and let the debounce decide";
  case ErrorCode::kTransientIntegrityCheckFailed:
    return "payload failed its integrity check; discard it and count it";
  case ErrorCode::kTransientPeerUnreachable:
    return "peer did not answer; membership decides whether it is down";
  case ErrorCode::kExternalNotConnected:
    return "external party is not connected; wait for it to attach";
  case ErrorCode::kExternalRequestRejected:
    return "external party refused the request; answer per the protocol";
  case ErrorCode::kExternalUnsupportedRequest:
    return "request is outside the supported set; answer with the protocol code";
  case ErrorCode::kInternalArithmeticOverflow:
    return "result does not fit its representation; bound the inputs";
  case ErrorCode::kInternalBufferTooSmall:
    return "buffer cannot hold the field; size it from the message layout";
  case ErrorCode::kInternalOutOfRange:
    return "index or offset is outside the addressed object";
  }
  return "unrecognised error code";
}

} // namespace volt::core
