// Dedicated-server machine ECS residency policy.
//
// This controller keeps machine runtimes materialized only for chunks covered
// by a permanent spawn ticket or authoritative player tickets. Chunks remain
// terrain-resident in this phase; only machine ECS ownership transfers to the
// offline sidecar simulation.

#pragma once

#include "core/expected.h"
#include "game/player/player_state.h"

#include <cstdint>
#include <span>

namespace snt::game {

class ScienceAndTheologySimulationSession;

struct ServerMachineChunkResidencyConfig {
    GamePlayerWorldPosition permanent_spawn;
    uint32_t horizontal_aoi_radius_blocks = 64;
    uint32_t vertical_aoi_radius_blocks = 64;
};

class ServerMachineChunkResidencyController final {
public:
    ServerMachineChunkResidencyController(
        ScienceAndTheologySimulationSession& simulation_session,
        ServerMachineChunkResidencyConfig config) noexcept;

    // Reconciles only at a server fixed-tick barrier. Callers supply copied
    // authoritative player positions so this module has no peer or ECS-player
    // ownership and remains straightforward to test without a transport.
    [[nodiscard]] snt::core::Expected<void> reconcile(
        uint64_t current_tick,
        std::span<const GamePlayerWorldPosition> active_player_positions);

private:
    ScienceAndTheologySimulationSession* simulation_session_ = nullptr;
    ServerMachineChunkResidencyConfig config_;
};

}  // namespace snt::game
