// Dedicated-server terrain ticket policy.
//
// This module converts the permanent spawn and authoritative player positions
// into bounded chunk tickets. The shared simulation session owns terrain I/O,
// offline-machine ownership, and persistence; this server layer deliberately
// has no direct ChunkRegistry or sidecar mutation access.

#pragma once

#include "core/expected.h"
#include "game/player/player_state.h"

#include <cstdint>
#include <span>

namespace snt::game {

class ScienceAndTheologySimulationSession;

struct ServerChunkTicketConfig {
    GamePlayerWorldPosition permanent_spawn;
    uint32_t horizontal_aoi_radius_blocks = 64;
    uint32_t vertical_aoi_radius_blocks = 64;
    uint32_t max_ticketed_chunks = 4096;
};

class ServerChunkTicketController final {
public:
    ServerChunkTicketController(
        ScienceAndTheologySimulationSession& simulation_session,
        ServerChunkTicketConfig config) noexcept;

    // Reconciles only at a server fixed-tick barrier. Callers supply copied
    // authoritative player positions so ticket policy remains independent of
    // peer ownership, ECS-player state, and transport implementation.
    [[nodiscard]] snt::core::Expected<void> reconcile(
        uint64_t current_tick,
        std::span<const GamePlayerWorldPosition> active_player_positions);

private:
    ScienceAndTheologySimulationSession* simulation_session_ = nullptr;
    ServerChunkTicketConfig config_;
};

}  // namespace snt::game
