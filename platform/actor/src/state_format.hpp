#pragma once

#include <cstddef>
#include <cstdint>

namespace volt::actor::detail {

// ASCII "VSTA" in a little-endian word. This marker is part of the P11 state
// format: changing it rejects every checkpoint written by an older runtime.
inline constexpr std::uint32_t kStateMagic = 0x4154'5356U;

// Four marker bytes followed by the 16-bit schema version defined by P11.
// Growing this header shifts every serialized actor field and therefore needs
// an explicit format migration rather than a local constant edit.
inline constexpr std::size_t kStateHeaderBytes = sizeof(kStateMagic) + sizeof(std::uint16_t);

} // namespace volt::actor::detail
