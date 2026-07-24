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
#include "game/automation/machine_automation_work_order.h"
#include "game/automation/ae_network_types.h"
#include "game/automation/sfm_flow_program.h"
#include "game/resources/resource_key.h"
#include "game/simulation/machine_fluid_tank.h"
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
struct MachineRuntimeRecipeSnapshot {
    std::string id;
    std::vector<ResourceContentStack> inputs;
    std::vector<ResourceContentStack> outputs;
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
    std::vector<ResourceContentStack> input_slots;
    std::vector<ResourceContentStack> output_slots;
    std::vector<MachineFluidTankRecord> fluid_tanks;

    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t max_input_slots = 4;
    int32_t max_output_slots = 4;
    int32_t max_stack_size = 64;

    int32_t progress_ticks = 0;
    std::optional<MachineRuntimeRecipeSnapshot> active_recipe;
    // An AE pattern provider may own one explicit machine operation. The
    // machine still owns all physical inputs, energy use, progress, and
    // output slots; this value only correlates that normal work to a stable
    // provider request until delivery is acknowledged.
    std::optional<MachineAutomationWorkOrderRecord> automation_work_order;
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

// Automation controllers are independent block owners.  They are not recipe
// machines and therefore must never be represented by MachineRuntimeComponent
// or machine_runtime_records.  The anchor carries spatial/world ownership;
// this typed record carries the durable SFM graph and future controller state.
enum class AutomationControllerKind : uint8_t {
    kSfmManager = 1,
    // Game-owned AE controller with a one-to-one physical topology node.
    // Its durable config lives with the typed AE node record rather than an
    // SFM flow graph.
    kAeController = 2,
};

struct AutomationControllerPersistenceRecord {
    // Must name a BlockEntityPlacement with entity_type ==
    // AUTOMATION_CONTROLLER in this sidecar.
    EntityId anchor_entity_id;
    AutomationControllerKind kind = AutomationControllerKind::kSfmManager;
    // Stable content identity. Runtime topology and UI presentation resolve
    // this key after load; compact ids never enter sidecars or save payloads.
    std::string controller_key;
    // Monotonic authoritative editor/presentation revision.  It is distinct
    // from SfmFlowProgramRecord::revision so a controller can later expose
    // other state without changing the graph's optimistic-edit base.
    uint64_t revision = 1;
    SfmFlowProgramRecord sfm_program;
};

// One voxel-anchored node in the physical AE cable topology. Controller nodes
// share their anchor with an AutomationControllerPersistenceRecord of kind
// kAeController. Every other node owns an AUTOMATION_NETWORK_NODE anchor.
// Edges are deliberately not persisted: active topology derives only from
// adjacent roots with compatible reciprocal port bits at materialize time.
struct AeNetworkNodePersistenceRecord {
    EntityId anchor_entity_id;
    // Stable authored topology/device key. It resolves drive cell behavior at
    // materialization time; compact runtime IDs never enter the sidecar.
    std::string node_key;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = true;
    // Zero selects the runtime default for a controller/channel provider.
    // Other node categories must preserve zero.
    int32_t provided_channels = 0;
    uint8_t connection_mask = CONN_ALL;
    uint64_t revision = 1;
};

// One drive owns one compact digital cell. Its capacity/filter configuration
// comes from the current AE node content key, while this durable record keeps
// only stable resource identities and amounts. Storage buses deliberately do
// not use this record: their future external endpoint binding has a separate
// owner contract.
struct AeDriveStoragePersistenceRecord {
    EntityId anchor_entity_id;
    uint64_t revision = 1;
    std::vector<ResourceContentStack> stored_resources;
};

// One AE interface can expose one real machine as a pattern provider.  The
// interface anchor owns the provider identity and serial allocator; the
// machine anchor owns the actual input, energy, progress, and output state.
// Keeping this relation in a typed sidecar record avoids embedding a runtime
// ECS handle or a process-local job id in either save data or AE topology.
struct AeMachinePatternProviderPersistenceRecord {
    EntityId interface_anchor_entity_id;
    EntityId machine_anchor_entity_id;
    bool enabled = true;
    int32_t priority = 0;
    // Monotonic serial allocated to the next accepted machine work order.
    // It starts at one and never reuses a value within this interface owner.
    uint64_t next_job_serial = 1;
    uint64_t revision = 1;
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

// kNone is required for non-fluid segments. It keeps liquid and gas graphs
// distinct even though both transport the durable `fluid` resource kind.
enum class OfflineNetworkFluidTransport : uint8_t {
    kNone = 0,
    kLiquid = 1,
    kGas = 2,
};

// A resource segment is a connected subgraph within one atomic offline
// island. A machine may bridge power, item, and fluid networks, but only the
// machines listed here may exchange the segment's resource. This prevents a
// pipe edge from accidentally joining two otherwise separate power grids.
struct OfflineNetworkTransportSegment {
    uint64_t segment_id = 0;
    OfflineNetworkResourceKind kind = OfflineNetworkResourceKind::kPower;
    OfflineNetworkFluidTransport fluid_transport = OfflineNetworkFluidTransport::kNone;
    std::vector<uint64_t> machine_guids;
    int64_t capacity = 0;
    int64_t max_transfer_per_tick = 0;
};

struct OfflineNetworkResourceLedger {
    // A ledger belongs to exactly one transport segment. `resource` is a
    // durable content-resource identity, while segment_id preserves the topology
    // boundary when several same-kind subnetworks are joined only by a
    // machine bridge. ResourceKey is intentionally never persisted
    // here because its numeric IDs are scoped to one content snapshot.
    uint64_t segment_id = 0;
    OfflineNetworkResourceKind kind = OfflineNetworkResourceKind::kPower;
    ResourceContentKey resource;
    int64_t stored_amount = 0;
    int64_t capacity = 0;
    // A topology provider derives this from the narrowest durable edge. Zero
    // means the ledger is storage-only and must not transfer between nodes.
    int64_t max_transfer_per_tick = 0;
};

struct OfflineNetworkBoundaryPort {
    uint64_t segment_id = 0;
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
    std::vector<OfflineNetworkTransportSegment> transport_segments;
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
    ResourceContentStack resource;
    std::string instance_data;

    [[nodiscard]] static GamePlayerGraveItemStack item(std::string id, int64_t count,
                                                        std::string variant = {},
                                                        std::string instance_data = {}) {
        return {
            .resource = ResourceContentStack::item(
                std::move(id), count, std::move(variant)),
            .instance_data = std::move(instance_data),
        };
    }
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

// Durable, chunk-owned physical item stack. Runtime inventories use compact
// ResourceKey values, while this record stays at the content-key persistence
// boundary so a saved world never depends on a process-local resource index.
struct GameGroundLootRecord {
    uint64_t loot_id = 0;
    ResourceContentStack resource;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    uint64_t spawned_tick = 0;
};

inline constexpr uint32_t kMaxGameGroundLootRecordsPerChunk = 4096;

struct GameChunkSidecar {
    std::vector<ConnectorPlacement> connectors;
    std::vector<MechanismPlacement> mechanisms;
    std::vector<EntityId> entities;
    std::vector<MachineRuntimePersistenceRecord> machine_runtime_records;
    std::vector<AutomationControllerPersistenceRecord> automation_controller_records;
    std::vector<AeNetworkNodePersistenceRecord> ae_network_node_records;
    std::vector<AeDriveStoragePersistenceRecord> ae_drive_storage_records;
    std::vector<AeMachinePatternProviderPersistenceRecord>
        ae_machine_pattern_provider_records;
    // Only an island's anchor chunk contains a record here. The registry
    // validates this against member machine ownership during world startup.
    std::vector<OfflineNetworkIslandSnapshot> offline_network_islands;
    std::vector<BlockEntityPlacement> block_entities;
    std::vector<TreeGrowthPersistenceRecord> tree_growth_records;
    std::vector<FarmlandPersistenceRecord> farmland_records;
    std::vector<CropGrowthPersistenceRecord> crop_growth_records;
    std::vector<GamePlayerBedRecord> player_beds;
    std::vector<GamePlayerGraveRecord> player_graves;
    // This value advances even after the chunk's loot is collected. Server
    // startup scans every sidecar to recover one world-wide non-reusing loot
    // serial allocator without introducing a second global save file.
    uint64_t next_ground_loot_serial = 1;
    std::vector<GameGroundLootRecord> ground_loot;
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
