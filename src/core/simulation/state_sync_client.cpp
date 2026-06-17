#include "state_sync_client.hpp"

namespace science_and_theology {

void StateSyncClient::apply_delta(const StateDelta& delta, WorldData* world) {
    if (!world) return;

    last_timestamp_ = delta.timestamp;

    for (const auto& key : delta.chunks_modified) {
        auto* chunk = world->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk) continue;

        if ((delta.flags & SyncFlags::TERRAIN) != SyncFlags::NONE) {
            // Terrain updated — mark chunk for re-render.
        }

        if ((delta.flags & SyncFlags::ENTITY) != SyncFlags::NONE) {
            // Entity changes — Godot rendering layer processes
            // entities_created / entities_destroyed to create/remove
            // EntityRenderProxy nodes.
        }

        if ((delta.flags & SyncFlags::MACHINE_STATE) != SyncFlags::NONE) {
            // Machine state changes — update visual state indicators.
            for (const auto& [eid, new_state] : delta.machine_state_changes) {
                // Rendering proxy updates machine visual state here.
            }
        }
    }
}

} // namespace science_and_theology
