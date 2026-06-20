#pragma once

#include <cstdint>

namespace science_and_theology {

// PlayerId — universal player identifier.
//
// In single-player mode, the local player is always PlayerId 1.
// In multiplayer mode, the server assigns a unique PlayerId to each
// connected client. PlayerId 0 is reserved as "invalid".
//
// This type lives in the engine-agnostic core so that snt_core,
// future snt_server, and the GDExtension binding layer all share
// the same player identity model.
using PlayerId = uint64_t;

inline constexpr PlayerId kInvalidPlayerId = 0;
inline constexpr PlayerId kSinglePlayerId = 1;

} // namespace science_and_theology
