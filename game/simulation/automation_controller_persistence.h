// Chunk-anchored automation-controller persistence boundary.
//
// This module owns SFM/AE controller anchors independently from processing
// machines.  A controller's typed state remains in GameChunkSidecar while
// runtime flow executors and AE topology are materialized by their owning
// automation service after a chunk becomes active.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

struct AutomationControllerAnchor {
    EntityId anchor_entity_id;
};

inline constexpr std::string_view kSfmManagerControllerKey = "automation.sfm_manager";
inline constexpr std::string_view kAeControllerKey = "automation.ae_controller";

struct AutomationAeControllerNodeConfig {
    bool enabled = true;
    int32_t provided_channels = 0;
    uint8_t connection_mask = CONN_ALL;
};

// A block placement supplies this complete durable identity. AE controller
// creation atomically adds its physical topology node without making a
// processing MachineRuntimeComponent own a controller block.
struct AutomationControllerCreateRequest {
    AutomationControllerKind kind = AutomationControllerKind::kSfmManager;
    std::string controller_key;
    SfmFlowProgramRecord sfm_program;
    AutomationAeControllerNodeConfig ae_node;
};

class GameAutomationControllerPersistence final {
public:
    // Creates one typed controller record and its AUTOMATION_CONTROLLER block
    // anchor atomically inside an existing chunk sidecar. The current SFM
    // request may contain an empty graph; an editor later replaces it through
    // replace_sfm_program.
    [[nodiscard]] static snt::core::Expected<AutomationControllerAnchor>
    create_controller(
        GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key,
        int32_t root_x,
        int32_t root_y,
        int32_t root_z,
        AutomationControllerCreateRequest request);

    // Replaces only the durable graph.  Endpoint addresses and content stacks
    // remain stable values here; the active chunk compiles them before use.
    [[nodiscard]] static snt::core::Expected<void> replace_sfm_program(
        GameChunkSidecarRegistry& sidecars,
        EntityId anchor_entity_id,
        SfmFlowProgramRecord program);

    // Removes both typed record and its matching block anchor.  It searches
    // sidecars only at a player/world mutation boundary, never in a tick loop.
    [[nodiscard]] static snt::core::Expected<void> remove_controller(
        GameChunkSidecarRegistry& sidecars,
        EntityId anchor_entity_id);

    // Startup/load validation.  It rejects duplicate anchors, invalid chunk
    // ownership, invalid controller kinds, and malformed graph identities
    // before an automation runtime can compile or execute them.
    [[nodiscard]] static snt::core::Expected<void> validate_all(
        const GameChunkSidecarRegistry& sidecars);
};

}  // namespace snt::game
