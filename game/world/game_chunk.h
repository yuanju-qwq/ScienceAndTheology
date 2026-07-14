// ScienceAndTheology chunk sidecars.
//
// VoxelChunk is engine-owned and contains only terrain. This file owns every
// ScienceAndTheology-specific persistent extension, keyed by the same generic
// ChunkKey. GameChunk is a transient assembly type used by world generation
// and serialization; Runtime stores only its VoxelChunk base.

#pragma once

#include "game/world/defs/block_entity.h"
#include "game/world/defs/captive_creature.h"
#include "game/world/defs/entity_data.h"
#include "game/world/defs/population_cell.h"
#include "game/world/voxel_primitives.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snt::game {

struct ConnectorPlacement {
    int64_t connector_id = 0;
    std::string from_dimension;
    int from_cell_x = 0;
    int from_cell_y = 0;
    int from_cell_z = 0;
    std::string to_dimension;
    int to_cell_x = 0;
    int to_cell_y = 0;
    int to_cell_z = 0;
    bool one_way = false;
    bool locked = false;
    std::string connector_type;
    int activation_mode = 0;
};

struct MechanismEffectPlacement {
    std::string effect_type;
    int64_t connector_id = 0;
    bool when_active = false;
    bool when_inactive = true;
};

struct MechanismPlacement {
    std::string mechanism_id;
    std::string dimension_id;
    int cell_x = 0;
    int cell_y = 0;
    int cell_z = 0;
    std::string title_key;
    std::string action_label;
    std::string flag_name;
    int activation_mode = 0;
    bool one_shot = true;
    std::string required_flag;
    std::vector<MechanismEffectPlacement> effects;
};

struct GameChunkSidecar {
    std::vector<ConnectorPlacement> connectors;
    std::vector<MechanismPlacement> mechanisms;
    std::vector<EntityId> entities;
    std::vector<MachineId> machines;
    std::vector<BlockEntityPlacement> block_entities;
    std::vector<ConnectorId> connector_ids;
    bool has_population_cell = false;
    PopulationCell population_cell{};
    bool has_captive_creatures = false;
    std::vector<CaptiveCreature> captive_creatures;
};

// A temporary aggregate for game-owned generation and persistence paths.
// Multiple inheritance is value-only here: callers explicitly extract the
// VoxelChunk and GameChunkSidecar bases before crossing the engine boundary.
struct GameChunk final : public VoxelChunk, public GameChunkSidecar {
    VoxelChunk& voxel_chunk() noexcept { return *this; }
    const VoxelChunk& voxel_chunk() const noexcept { return *this; }
    GameChunkSidecar& sidecar() noexcept { return *this; }
    const GameChunkSidecar& sidecar() const noexcept { return *this; }
};

class GameChunkSidecarRegistry {
public:
    GameChunkSidecar* get(const ChunkKey& key) {
        const auto it = sidecars_.find(key);
        return it == sidecars_.end() ? nullptr : &it->second;
    }

    const GameChunkSidecar* get(const ChunkKey& key) const {
        const auto it = sidecars_.find(key);
        return it == sidecars_.end() ? nullptr : &it->second;
    }

    void set(ChunkKey key, GameChunkSidecar sidecar) {
        sidecars_[std::move(key)] = std::move(sidecar);
    }

    void remove(const ChunkKey& key) { sidecars_.erase(key); }
    void clear() { sidecars_.clear(); }
    size_t size() const noexcept { return sidecars_.size(); }

private:
    std::unordered_map<ChunkKey, GameChunkSidecar> sidecars_;
};

}  // namespace snt::game
