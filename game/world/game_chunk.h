// ScienceAndTheology chunk sidecars.
//
// VoxelChunk is engine-owned and contains only terrain. This file owns every
// ScienceAndTheology-specific persistent extension, keyed by the same generic
// ChunkKey. GameChunk is a transient assembly type used by world generation
// and serialization; Runtime stores only its VoxelChunk base.

#pragma once

#include "game/world/defs/block_entity.h"
#include "game/world/defs/captive_creature.h"
#include "game/world/defs/crop_species_def.h"
#include "game/world/defs/entity_data.h"
#include "game/world/defs/population_cell.h"
#include "game/resources/resource_key.h"
#include "game/world/voxel_primitives.h"

#include <cstdint>
#include <optional>
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

// Machine persistence is anchored to a MACHINE BlockEntityPlacement in the
// same chunk sidecar. These values deliberately mirror only durable machine
// state: they do not include ECS handles, script VM objects, or callbacks.
struct MachineRuntimeItemStack {
    ResourceStack resource;
};

struct MachineRuntimeRecipeSnapshot {
    std::string id;
    std::vector<MachineRuntimeItemStack> inputs;
    std::vector<MachineRuntimeItemStack> outputs;
    int32_t duration_ticks = 0;
    int32_t energy_per_tick = 0;
};

// A machine's durable record has exactly one writer. kLoaded is owned by its
// materialized ECS runtime, while the remaining modes are owned by the chunk
// sidecar/offline service after that runtime has been removed.
enum class MachineRuntimeResidency : uint8_t {
    kLoaded = 0,
    kPaused = 1,
    kOfflineStandalone = 2,
    kOfflineNetworkIsland = 3,
};

struct MachineRuntimePersistenceRecord {
    // Must name a BlockEntityPlacement with entity_type == MACHINE in this
    // sidecar. The placement's root cell establishes chunk ownership.
    EntityId anchor_entity_id;

    // EntityGuid is stored as its raw value so the game-world data target does
    // not depend on the ECS module. The simulation lifecycle reconstructs the
    // actual EntityGuid component after validating this record.
    uint64_t entity_guid = 0;

    std::string machine_id;
    std::vector<MachineRuntimeItemStack> input_slots;
    std::vector<MachineRuntimeItemStack> output_slots;

    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t max_input_slots = 4;
    int32_t max_output_slots = 4;
    int32_t max_stack_size = 64;

    int32_t progress_ticks = 0;
    std::optional<MachineRuntimeRecipeSnapshot> active_recipe;
    bool activation_requested = false;
    // Stable authenticated account that started the pending/manual job. This
    // survives a controlled restart so completion can emit a task event to
    // the correct player without storing a transport peer or ECS handle.
    std::string job_owner_account_id;
    uint8_t run_state = 0;

    // Offline ownership metadata. A non-loaded mode must never have a live
    // MachineRuntimeComponent at the same time. Network-island fields remain
    // zero for standalone machines and reserve a stable cross-chunk boundary.
    MachineRuntimeResidency residency = MachineRuntimeResidency::kLoaded;
    uint64_t offline_last_simulated_tick = 0;
    uint64_t offline_island_id = 0;
    uint64_t offline_epoch = 0;
};

// A network island is anchored by its lexicographically first member chunk.
// The anchor sidecar owns the complete compressed network state; every other
// member chunk retains only the machine-level island id and ownership epoch.
// This keeps per-chunk saves independent without duplicating a mutable
// resource ledger across region blobs.
enum class OfflineNetworkResourceKind : uint8_t {
    kPower = 0,
    kItem = 1,
    kFluid = 2,
};

struct OfflineNetworkResourceLedger {
    OfflineNetworkResourceKind kind = OfflineNetworkResourceKind::kPower;
    std::string resource_id;
    int64_t stored_amount = 0;
    int64_t capacity = 0;
};

struct OfflineNetworkBoundaryPort {
    uint64_t node_id = 0;
    ChunkKey adjacent_chunk;
    uint8_t direction = 0;
    uint64_t topology_revision = 0;
};

// The snapshot contains value-only topology compression. It never owns a
// terrain chunk, ECS entity, pipe segment, or cable segment; those remain
// reconstructible from normal chunk sidecars when the island materializes.
struct OfflineNetworkIslandSnapshot {
    uint64_t island_id = 0;
    uint64_t ownership_epoch = 0;
    std::string dimension_id;
    ChunkKey anchor_chunk;
    std::vector<ChunkKey> member_chunks;
    uint64_t topology_revision = 0;
    uint64_t last_simulated_tick = 0;
    std::vector<uint64_t> machine_guids;
    std::vector<OfflineNetworkBoundaryPort> boundary_ports;
    std::vector<OfflineNetworkResourceLedger> ledgers;
};

// Tree growth uses a typed sidecar record instead of the legacy
// BlockEntityPlacement::type_data_json payload. The anchor remains the
// authoritative identity and root coordinate; this record owns only the
// state that changes while the tree grows.
struct TreeGrowthOwnedCell {
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    TerrainMaterialId material = 0;
};

struct TreeGrowthPersistenceRecord {
    EntityId anchor_entity_id;
    std::string species_key;
    TreeGrowthStage growth_stage = TreeGrowthStage::SAPLING;
    uint64_t planted_tick = 0;
    uint64_t last_growth_tick = 0;
    std::vector<TreeGrowthOwnedCell> owned_cells;
};

// Crop and farmland state follows the same ownership model as tree growth:
// generic block anchors establish identity and root coordinates, while this
// typed sidecar owns every value that changes during simulation. Crop records
// reference their supporting farmland anchor when they are player-planted;
// a zero farmland anchor is reserved for a future explicitly modeled wild
// crop source.
struct FarmlandPersistenceRecord {
    EntityId anchor_entity_id;
    float moisture = 0.5f;
    float fertility = 0.7f;
    std::string last_crop_key;
    uint32_t consecutive_same_crop = 0;
    uint64_t last_moisture_tick = 0;
};

struct CropGrowthOwnedCell {
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    TerrainMaterialId material = 0;
};

struct CropGrowthPersistenceRecord {
    EntityId anchor_entity_id;
    EntityId farmland_anchor_entity_id;
    std::string species_key;
    CropGrowthStage growth_stage = CropGrowthStage::SEED;
    uint64_t planted_tick = 0;
    uint64_t last_growth_tick = 0;
    uint64_t last_harvest_tick = 0;
    bool is_regrowing = false;
    std::vector<CropGrowthOwnedCell> owned_cells;
};

// Player beds and graves are world-owned sidecar values. They intentionally
// do not use player ECS types: account-backed inventory conversion happens in
// the dedicated-server service, while sidecars remain reusable world data.
struct GamePlayerBedRecord {
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
};

struct GamePlayerGraveItemStack {
    ResourceStack resource;
    std::string instance_data;
};

struct GamePlayerGraveRecord {
    uint64_t grave_id = 0;
    std::string owner_account_id;
    uint64_t death_tick = 0;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    std::vector<GamePlayerGraveItemStack> items;
};

struct GameChunkSidecar {
    std::vector<ConnectorPlacement> connectors;
    std::vector<MechanismPlacement> mechanisms;
    std::vector<EntityId> entities;
    std::vector<MachineRuntimePersistenceRecord> machine_runtime_records;
    // Only an island's anchor chunk contains a record here. The registry
    // validates this against member machine ownership during world startup.
    std::vector<OfflineNetworkIslandSnapshot> offline_network_islands;
    std::vector<BlockEntityPlacement> block_entities;
    std::vector<TreeGrowthPersistenceRecord> tree_growth_records;
    std::vector<FarmlandPersistenceRecord> farmland_records;
    std::vector<CropGrowthPersistenceRecord> crop_growth_records;
    std::vector<GamePlayerBedRecord> player_beds;
    std::vector<GamePlayerGraveRecord> player_graves;
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

    template <typename Visitor>
    void for_each(Visitor&& visitor) {
        for (auto& [key, sidecar] : sidecars_) visitor(key, sidecar);
    }

    template <typename Visitor>
    void for_each(Visitor&& visitor) const {
        for (const auto& [key, sidecar] : sidecars_) visitor(key, sidecar);
    }

private:
    std::unordered_map<ChunkKey, GameChunkSidecar> sidecars_;
};

}  // namespace snt::game
