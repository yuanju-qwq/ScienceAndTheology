#pragma once

#include <cstdint>

namespace science_and_theology {

// PlayerHandle — runtime player handle.
//
// In single-player mode, the local player is always PlayerHandle 1.
// In multiplayer mode, the server assigns a unique PlayerHandle to each
// connected client. PlayerHandle 0 is reserved as "invalid".
//
// This type is only a process-local runtime handle for hot-path tables
// and network routing. Persistent player identity is player_uuid.
using PlayerHandle = uint64_t;

inline constexpr PlayerHandle kInvalidPlayerHandle = 0;
inline constexpr PlayerHandle kSinglePlayerHandle = 1;

} // namespace science_and_theology
