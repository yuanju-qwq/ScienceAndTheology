// Durable owner for AE interface-to-machine pattern-provider bindings.
//
// The interface sidecar owns enablement, priority, and the next work-order
// serial.  Machine sidecars remain the sole owner of physical machine state.
// This module validates and mutates only durable values; active provider
// endpoints are materialized separately by AeMachinePatternProviderRuntimeService.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"

#include <cstdint>

namespace snt::game {

class GameContentRegistry;

struct AeMachinePatternProviderBindingAnchor {
    EntityId interface_anchor_entity_id;
};

class GameAeMachinePatternProviderPersistence final {
public:
    // Creates the durable relation in the sidecar that owns interface_anchor.
    // A machine may be exposed by at most one interface, which preserves one
    // unambiguous owner for every in-flight MachineAutomationWorkOrder.
    [[nodiscard]] static snt::core::Expected<AeMachinePatternProviderBindingAnchor>
    create_binding(GameChunkSidecarRegistry& sidecars,
                   const ChunkKey& interface_chunk,
                   EntityId interface_anchor_entity_id,
                   EntityId machine_anchor_entity_id,
                   int32_t priority = 0);

    [[nodiscard]] static snt::core::Expected<void> remove_binding(
        GameChunkSidecarRegistry& sidecars, EntityId interface_anchor_entity_id);

    // Structural validation is independent of live content and rejects
    // dangling anchors, duplicate ownership, serial reuse, and orphaned
    // machine work orders before a world is materialized.
    [[nodiscard]] static snt::core::Expected<void> validate_all(
        const GameChunkSidecarRegistry& sidecars);

    // Current content validation runs at session/content boundaries.  Manual
    // machines deliberately cannot be exposed by AE providers, and every
    // persisted work order must still name the recipe/output promise it owns.
    [[nodiscard]] static snt::core::Expected<void> validate_content_references(
        const GameChunkSidecarRegistry& sidecars,
        const GameContentRegistry& content);
};

}  // namespace snt::game
