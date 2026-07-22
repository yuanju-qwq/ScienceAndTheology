// Chunk-anchored AE physical-node persistence boundary.
//
// AE controller anchors are created by GameAutomationControllerPersistence;
// this module owns every other node anchor. Durable records carry only spatial
// identity and port configuration. The active AeNetworkRuntimeService derives
// cable edges and runtime handles after chunks materialize.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"

#include <cstdint>
#include <string>

namespace snt::game {

class GameContentRegistry;

struct AeNetworkNodeAnchor {
    EntityId anchor_entity_id;
};

struct AeNetworkNodeCreateRequest {
    // Stable content identity copied into the typed sidecar record. Runtime
    // topology never hashes or compares this string after materialization.
    std::string node_key;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = true;
    int32_t provided_channels = 0;
    uint8_t connection_mask = CONN_ALL;
};

class GameAeNetworkPersistence final {
public:
    // Creates a non-controller physical AE node and its typed block owner.
    // Controller nodes must be created through GameAutomationControllerPersistence
    // so their controller identity and AE topology record remain atomic.
    [[nodiscard]] static snt::core::Expected<AeNetworkNodeAnchor> create_node(
        GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key,
        int32_t root_x,
        int32_t root_y,
        int32_t root_z,
        AeNetworkNodeCreateRequest request);

    // Removes one non-controller node and its matching block anchor. This is
    // a mutation-boundary scan; materialized runtimes keep O(1) anchor lookup.
    [[nodiscard]] static snt::core::Expected<void> remove_node(
        GameChunkSidecarRegistry& sidecars, EntityId anchor_entity_id);

    // Startup/load validation for all typed AE node owners, including the
    // one-to-one association between every AE controller and controller node.
    [[nodiscard]] static snt::core::Expected<void> validate_all(
        const GameChunkSidecarRegistry& sidecars);

    // Resolves every persisted non-controller node through the current
    // stable content key.  This runs at world/content boundaries; active AE
    // topology stores only compact node categories and never hashes keys.
    [[nodiscard]] static snt::core::Expected<void> validate_content_references(
        const GameChunkSidecarRegistry& sidecars,
        const GameContentRegistry& content);
};

}  // namespace snt::game
