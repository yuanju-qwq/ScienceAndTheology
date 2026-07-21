#include "chunk_serializer.h"

#include <cmath>
#include <cstring>

#include "game/world/defs/creature_species.h"

namespace snt::game {
namespace {

constexpr uint32_t kMaxMachineRuntimeRecords = 4096;
constexpr uint32_t kMaxMachineInputSlots = 64;
constexpr uint32_t kMaxMachineOutputSlots = 64;
constexpr uint32_t kMaxMachineRecipeInputs = 64;
constexpr uint32_t kMaxMachineRecipeOutputs = 64;
constexpr uint8_t kMachineRunStateCount = 6;
constexpr uint32_t kMaxMachineJobOwnerAccountBytes = 256;
constexpr uint32_t kMaxOfflineNetworkIslands = 1024;
constexpr uint32_t kMaxOfflineNetworkMemberChunks = 4096;
constexpr uint32_t kMaxOfflineNetworkMachineGuids = 16384;
constexpr uint32_t kMaxOfflineNetworkBoundaryPorts = 16384;
constexpr uint32_t kMaxOfflineNetworkLedgers = 1024;
constexpr uint32_t kMaxOfflineNetworkResourceIdBytes = 256;
constexpr uint32_t kMaxOfflineNetworkDimensionIdBytes = 256;
constexpr uint32_t kMaxTreeGrowthRecords = 4096;
constexpr uint32_t kMaxTreeGrowthOwnedCells = 4096;
constexpr uint32_t kMaxTreeGrowthSpeciesKeyBytes = 256;
constexpr uint32_t kMaxFarmlandRecords = 4096;
constexpr uint32_t kMaxCropGrowthRecords = 4096;
constexpr uint32_t kMaxCropGrowthOwnedCells = 64;
constexpr uint32_t kMaxCropGrowthSpeciesKeyBytes = 256;
constexpr uint32_t kMaxFarmlandCropKeyBytes = 256;
constexpr uint32_t kMaxPlayerBedRecords = 4096;
constexpr uint32_t kMaxPlayerGraveRecords = 4096;
constexpr uint32_t kMaxPlayerGraveItemStacks = 128;

}  // namespace

// --- Serialize ---

std::vector<uint8_t> GameChunkSerializer::serialize(
    const std::string& dimension_id, const GameChunk& chunk) const {
    std::vector<uint8_t> buf;

    // Header.
    write_uint8(buf, kCurrentVersion);
    write_int32(buf, chunk.chunk_x);
    write_int32(buf, chunk.chunk_y);
    write_int32(buf, chunk.chunk_z);
    write_uint8(buf, static_cast<uint8_t>(chunk.state));
    write_string(buf, dimension_id);

    // Terrain.
    int size_x = chunk.terrain.size_x;
    int size_y = chunk.terrain.size_y;
    int size_z = chunk.terrain.size_z;
    int cell_count = size_x * size_y * size_z;
    write_uint32(buf, static_cast<uint32_t>(size_x));
    write_uint32(buf, static_cast<uint32_t>(size_y));
    write_uint32(buf, static_cast<uint32_t>(size_z));
    write_uint32(buf, static_cast<uint32_t>(cell_count));

    // Materials (uint8 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint8(buf, static_cast<uint8_t>(chunk.terrain.cells[i].material));
    }

    // Flags (uint32 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint32(buf, chunk.terrain.cells[i].flags);
    }

    // Fluid values are stored separately from material/flags so a fluid cell
    // remains an air terrain cell with deterministic mass and temperature.
    for (int i = 0; i < cell_count; ++i) {
        const TerrainCell& cell = chunk.terrain.cells[i];
        write_uint16(buf, cell.fluid_type);
        write_int16(buf, cell.fluid_mass);
        write_int16(buf, cell.fluid_temperature);
        write_uint8(buf, cell.fluid_is_gas ? 1 : 0);
    }

    // Connectors.
    write_uint32(buf, static_cast<uint32_t>(chunk.connectors.size()));
    for (const auto& conn : chunk.connectors) {
        write_connector(buf, conn);
    }

    // Mechanisms.
    write_uint32(buf, static_cast<uint32_t>(chunk.mechanisms.size()));
    for (const auto& mechanism : chunk.mechanisms) {
        write_mechanism(buf, mechanism);
    }

    // Entity IDs.
    write_uint32(buf, static_cast<uint32_t>(chunk.entities.size()));
    for (const auto& eid : chunk.entities) {
        write_uint64(buf, eid.id);
    }

    // Machine runtime records. Each record is anchored to a MACHINE block
    // entity in this chunk; a bare machine id is not enough to restore it.
    write_uint32(buf, static_cast<uint32_t>(chunk.machine_runtime_records.size()));
    for (const auto& record : chunk.machine_runtime_records) {
        write_machine_runtime_record(buf, record);
    }

    // Every network island is written only by its anchor sidecar. Member
    // chunks reference it through MachineRuntimePersistenceRecord metadata.
    write_uint32(buf, static_cast<uint32_t>(chunk.offline_network_islands.size()));
    for (const OfflineNetworkIslandSnapshot& snapshot : chunk.offline_network_islands) {
        write_offline_network_island_snapshot(buf, snapshot);
    }

    // Connector IDs.
    write_uint32(buf,
                 static_cast<uint32_t>(chunk.connector_ids.size()));
    for (const auto& cid : chunk.connector_ids) {
        write_uint64(buf, cid.id);
    }

    // Block entities.
    write_uint32(buf, static_cast<uint32_t>(chunk.block_entities.size()));
    for (const auto& be : chunk.block_entities) {
        write_block_entity(buf, be);
    }

    // Typed tree-growth state is kept separate from generic block anchors so
    // terrain systems never need to parse an opaque JSON/string payload.
    write_uint32(buf, static_cast<uint32_t>(chunk.tree_growth_records.size()));
    for (const TreeGrowthPersistenceRecord& record : chunk.tree_growth_records) {
        write_tree_growth_record(buf, record);
    }

    // Crop runtime values are separate from their generic CROP/FARMLAND
    // anchors for the same reason as tree growth: no gameplay code parses a
    // legacy opaque block-entity payload.
    write_uint32(buf, static_cast<uint32_t>(chunk.farmland_records.size()));
    for (const FarmlandPersistenceRecord& record : chunk.farmland_records) {
        write_farmland_record(buf, record);
    }
    write_uint32(buf, static_cast<uint32_t>(chunk.crop_growth_records.size()));
    for (const CropGrowthPersistenceRecord& record : chunk.crop_growth_records) {
        write_crop_growth_record(buf, record);
    }

    // Player bed anchors and grave inventories are game-owned world values.
    write_uint32(buf, static_cast<uint32_t>(chunk.player_beds.size()));
    for (const GamePlayerBedRecord& record : chunk.player_beds) {
        write_player_bed_record(buf, record);
    }
    write_uint32(buf, static_cast<uint32_t>(chunk.player_graves.size()));
    for (const GamePlayerGraveRecord& record : chunk.player_graves) {
        write_player_grave_record(buf, record);
    }

    // Population cell (ecosystem data, version 6+).
    write_uint8(buf, chunk.has_population_cell ? 1 : 0);
    if (chunk.has_population_cell) {
        write_population_cell(buf, chunk.population_cell);
    }

    // Captive creatures (husbandry data, version 8+).
    write_uint8(buf, chunk.has_captive_creatures ? 1 : 0);
    if (chunk.has_captive_creatures) {
        write_uint32(buf, static_cast<uint32_t>(chunk.captive_creatures.size()));
        for (const auto& cc : chunk.captive_creatures) {
            write_captive_creature(buf, cc);
        }
    }

    return buf;
}

// --- Deserialize ---

bool GameChunkSerializer::deserialize(
    const std::vector<uint8_t>& data,
    std::string& dimension_id, GameChunk& chunk) const {
    size_t offset = 0;

    uint8_t version;
    if (!read_uint8(data, offset, version)) return false;
    if (version != kCurrentVersion) return false;

    int32_t cx, cy, cz;
    if (!read_int32(data, offset, cx)) return false;
    if (!read_int32(data, offset, cy)) return false;
    if (!read_int32(data, offset, cz)) return false;
    chunk.chunk_x = cx;
    chunk.chunk_y = cy;
    chunk.chunk_z = cz;

    uint8_t state_byte;
    if (!read_uint8(data, offset, state_byte)) return false;
    chunk.state = static_cast<ChunkState>(state_byte);

    if (!read_string(data, offset, dimension_id)) return false;

    // Terrain.
    uint32_t sx, sy, sz, cc;
    if (!read_uint32(data, offset, sx)) return false;
    if (!read_uint32(data, offset, sy)) return false;
    if (!read_uint32(data, offset, sz)) return false;
    if (!read_uint32(data, offset, cc)) return false;

    int size_x = static_cast<int>(sx);
    int size_y = static_cast<int>(sy);
    int size_z = static_cast<int>(sz);
    int cell_count = static_cast<int>(cc);

    if (cell_count != size_x * size_y * size_z) return false;
    if (cell_count <= 0 ||
        cell_count > VoxelChunk::kChunkSize * VoxelChunk::kChunkSize * VoxelChunk::kChunkSize) {
        return false;
    }

    chunk.terrain.resize(size_x, size_y, size_z);

    // Materials.
    for (int i = 0; i < cell_count; ++i) {
        uint8_t mat;
        if (!read_uint8(data, offset, mat)) return false;
        chunk.terrain.cells[i].material = static_cast<TerrainMaterial>(mat);
    }

    // Flags.
    for (int i = 0; i < cell_count; ++i) {
        uint32_t flags;
        if (!read_uint32(data, offset, flags)) return false;
        chunk.terrain.cells[i].flags = flags;
    }

    // Fluid values are a latest-only v16 layout. Invalid mass/type pairs are
    // rejected before sidecars or gameplay state can observe the chunk.
    for (int i = 0; i < cell_count; ++i) {
        TerrainCell& cell = chunk.terrain.cells[i];
        uint8_t is_gas = 0;
        if (!read_uint16(data, offset, cell.fluid_type) ||
            !read_int16(data, offset, cell.fluid_mass) ||
            !read_int16(data, offset, cell.fluid_temperature) ||
            !read_uint8(data, offset, is_gas) || is_gas > 1 ||
            cell.fluid_mass < 0 || cell.fluid_mass > snt::voxel::kCellFluidCapacity ||
            (cell.fluid_mass == 0 &&
             (cell.fluid_type != snt::voxel::kInvalidCellFluidId || is_gas != 0)) ||
            (cell.fluid_mass > 0 &&
             cell.fluid_type == snt::voxel::kInvalidCellFluidId)) {
            return false;
        }
        cell.fluid_is_gas = is_gas != 0;
    }

    // Connectors.
    uint32_t conn_count;
    if (!read_uint32(data, offset, conn_count)) return false;
    chunk.connectors.clear();
    chunk.connectors.reserve(conn_count);
    for (uint32_t i = 0; i < conn_count; ++i) {
        ConnectorPlacement conn;
        if (!read_connector(data, offset, conn)) return false;
        chunk.connectors.push_back(std::move(conn));
    }

    // Mechanisms.
    chunk.mechanisms.clear();
    uint32_t mechanism_count;
    if (!read_uint32(data, offset, mechanism_count)) return false;
    chunk.mechanisms.reserve(mechanism_count);
    for (uint32_t i = 0; i < mechanism_count; ++i) {
        MechanismPlacement mechanism;
        if (!read_mechanism(data, offset, mechanism)) return false;
        chunk.mechanisms.push_back(std::move(mechanism));
    }

    // Entity IDs.
    uint32_t entity_count;
    if (!read_uint32(data, offset, entity_count)) return false;
    chunk.entities.clear();
    chunk.entities.reserve(entity_count);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.entities.push_back(EntityId{id});
    }

    // Machine runtime records.
    uint32_t machine_record_count;
    if (!read_uint32(data, offset, machine_record_count)) return false;
    if (machine_record_count > kMaxMachineRuntimeRecords) return false;
    chunk.machine_runtime_records.clear();
    chunk.machine_runtime_records.reserve(machine_record_count);
    for (uint32_t i = 0; i < machine_record_count; ++i) {
        MachineRuntimePersistenceRecord record;
        if (!read_machine_runtime_record(data, offset, record)) return false;
        chunk.machine_runtime_records.push_back(std::move(record));
    }

    uint32_t offline_network_island_count;
    if (!read_uint32(data, offset, offline_network_island_count) ||
        offline_network_island_count > kMaxOfflineNetworkIslands) {
        return false;
    }
    chunk.offline_network_islands.clear();
    chunk.offline_network_islands.reserve(offline_network_island_count);
    for (uint32_t i = 0; i < offline_network_island_count; ++i) {
        OfflineNetworkIslandSnapshot snapshot;
        if (!read_offline_network_island_snapshot(data, offset, snapshot)) return false;
        chunk.offline_network_islands.push_back(std::move(snapshot));
    }

    // Connector IDs.
    uint32_t conn_id_count;
    if (!read_uint32(data, offset, conn_id_count)) return false;
    chunk.connector_ids.clear();
    chunk.connector_ids.reserve(conn_id_count);
    for (uint32_t i = 0; i < conn_id_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.connector_ids.push_back(ConnectorId{id});
    }

    // Block entities.
    uint32_t be_count;
    if (!read_uint32(data, offset, be_count)) return false;
    chunk.block_entities.clear();
    chunk.block_entities.reserve(be_count);
    for (uint32_t i = 0; i < be_count; ++i) {
        BlockEntityPlacement be;
        if (!read_block_entity(data, offset, be)) return false;
        chunk.block_entities.push_back(std::move(be));
    }

    uint32_t tree_growth_record_count;
    if (!read_uint32(data, offset, tree_growth_record_count) ||
        tree_growth_record_count > kMaxTreeGrowthRecords) {
        return false;
    }
    chunk.tree_growth_records.clear();
    chunk.tree_growth_records.reserve(tree_growth_record_count);
    for (uint32_t i = 0; i < tree_growth_record_count; ++i) {
        TreeGrowthPersistenceRecord record;
        if (!read_tree_growth_record(data, offset, record)) return false;
        chunk.tree_growth_records.push_back(std::move(record));
    }

    uint32_t farmland_record_count;
    if (!read_uint32(data, offset, farmland_record_count) ||
        farmland_record_count > kMaxFarmlandRecords) {
        return false;
    }
    chunk.farmland_records.clear();
    chunk.farmland_records.reserve(farmland_record_count);
    for (uint32_t i = 0; i < farmland_record_count; ++i) {
        FarmlandPersistenceRecord record;
        if (!read_farmland_record(data, offset, record)) return false;
        chunk.farmland_records.push_back(std::move(record));
    }

    uint32_t crop_growth_record_count;
    if (!read_uint32(data, offset, crop_growth_record_count) ||
        crop_growth_record_count > kMaxCropGrowthRecords) {
        return false;
    }
    chunk.crop_growth_records.clear();
    chunk.crop_growth_records.reserve(crop_growth_record_count);
    for (uint32_t i = 0; i < crop_growth_record_count; ++i) {
        CropGrowthPersistenceRecord record;
        if (!read_crop_growth_record(data, offset, record)) return false;
        chunk.crop_growth_records.push_back(std::move(record));
    }

    // Player bed anchors and grave inventories.
    uint32_t bed_count;
    if (!read_uint32(data, offset, bed_count) || bed_count > kMaxPlayerBedRecords) {
        return false;
    }
    chunk.player_beds.clear();
    chunk.player_beds.reserve(bed_count);
    for (uint32_t i = 0; i < bed_count; ++i) {
        GamePlayerBedRecord record;
        if (!read_player_bed_record(data, offset, record)) return false;
        chunk.player_beds.push_back(std::move(record));
    }

    uint32_t grave_count;
    if (!read_uint32(data, offset, grave_count) || grave_count > kMaxPlayerGraveRecords) {
        return false;
    }
    chunk.player_graves.clear();
    chunk.player_graves.reserve(grave_count);
    for (uint32_t i = 0; i < grave_count; ++i) {
        GamePlayerGraveRecord record;
        if (!read_player_grave_record(data, offset, record)) return false;
        chunk.player_graves.push_back(std::move(record));
    }

    // Population cell.
    uint8_t has_pop;
    if (!read_uint8(data, offset, has_pop)) return false;
    chunk.has_population_cell = (has_pop != 0);
    if (chunk.has_population_cell) {
        if (!read_population_cell(data, offset, chunk.population_cell)) {
            return false;
        }
    }

    // Captive creatures.
    uint8_t has_captive;
    if (!read_uint8(data, offset, has_captive)) return false;
    chunk.has_captive_creatures = (has_captive != 0);
    chunk.captive_creatures.clear();
    if (chunk.has_captive_creatures) {
        uint32_t captive_count;
        if (!read_uint32(data, offset, captive_count)) return false;
        // Sanity cap to avoid corrupt data causing huge allocations.
        if (captive_count > 4096) return false;
        chunk.captive_creatures.resize(captive_count);
        for (uint32_t i = 0; i < captive_count; ++i) {
            if (!read_captive_creature(data, offset,
                    chunk.captive_creatures[i])) {
                return false;
            }
        }
    }

    return offset == data.size();
}

// --- Write helpers ---

void GameChunkSerializer::write_uint8(std::vector<uint8_t>& buf, uint8_t value) {
    buf.push_back(value);
}

void GameChunkSerializer::write_int16(std::vector<uint8_t>& buf, int16_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_uint16(std::vector<uint8_t>& buf, uint16_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_int32(std::vector<uint8_t>& buf, int32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_uint32(std::vector<uint8_t>& buf, uint32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_int64(std::vector<uint8_t>& buf, int64_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_uint64(std::vector<uint8_t>& buf, uint64_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_float(std::vector<uint8_t>& buf, float value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void GameChunkSerializer::write_string(std::vector<uint8_t>& buf,
                                   const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_uint32(buf, len);
    if (len > 0) {
        const auto* data = reinterpret_cast<const uint8_t*>(str.data());
        buf.insert(buf.end(), data, data + len);
    }
}

void GameChunkSerializer::write_bytes(std::vector<uint8_t>& buf,
                                  const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// --- Read helpers ---

bool GameChunkSerializer::read_uint8(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t& out) {
    if (offset >= data.size()) return false;
    out = data[offset++];
    return true;
}

bool GameChunkSerializer::read_int16(const std::vector<uint8_t>& data,
                                     size_t& offset, int16_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_uint16(const std::vector<uint8_t>& data,
                                      size_t& offset, uint16_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_int32(const std::vector<uint8_t>& data,
                                 size_t& offset, int32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_uint32(const std::vector<uint8_t>& data,
                                  size_t& offset, uint32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_int64(const std::vector<uint8_t>& data,
                                 size_t& offset, int64_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_uint64(const std::vector<uint8_t>& data,
                                  size_t& offset, uint64_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_float(const std::vector<uint8_t>& data,
                                 size_t& offset, float& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool GameChunkSerializer::read_string(const std::vector<uint8_t>& data,
                                  size_t& offset, std::string& out) {
    uint32_t len;
    if (!read_uint32(data, offset, len)) return false;
    if (offset + len > data.size()) return false;
    out.assign(reinterpret_cast<const char*>(&data[offset]), len);
    offset += len;
    return true;
}

bool GameChunkSerializer::read_bytes(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t* out, size_t len) {
    if (offset + len > data.size()) return false;
    std::memcpy(out, &data[offset], len);
    offset += len;
    return true;
}

// --- Connector helpers ---

void GameChunkSerializer::write_connector(std::vector<uint8_t>& buf,
                                      const ConnectorPlacement& conn) {
    write_uint64(buf, static_cast<uint64_t>(conn.connector_id));
    write_string(buf, conn.from_dimension);
    write_int32(buf, conn.from_cell_x);
    write_int32(buf, conn.from_cell_y);
    write_int32(buf, conn.from_cell_z);
    write_string(buf, conn.to_dimension);
    write_int32(buf, conn.to_cell_x);
    write_int32(buf, conn.to_cell_y);
    write_int32(buf, conn.to_cell_z);
    write_uint8(buf, conn.one_way ? 1 : 0);
    write_uint8(buf, conn.locked ? 1 : 0);
    write_string(buf, conn.connector_type);
    write_uint8(buf, static_cast<uint8_t>(conn.activation_mode));
}

bool GameChunkSerializer::read_connector(const std::vector<uint8_t>& data,
                                      size_t& offset,
                                      ConnectorPlacement& conn) {
    uint64_t raw_id;
    if (!read_uint64(data, offset, raw_id)) return false;
    conn.connector_id = static_cast<int64_t>(raw_id);
    if (!read_string(data, offset, conn.from_dimension)) return false;
    if (!read_int32(data, offset, conn.from_cell_x)) return false;
    if (!read_int32(data, offset, conn.from_cell_y)) return false;
    if (!read_int32(data, offset, conn.from_cell_z)) return false;
    if (!read_string(data, offset, conn.to_dimension)) return false;
    if (!read_int32(data, offset, conn.to_cell_x)) return false;
    if (!read_int32(data, offset, conn.to_cell_y)) return false;
    if (!read_int32(data, offset, conn.to_cell_z)) return false;

    uint8_t ow, lk, am;
    if (!read_uint8(data, offset, ow)) return false;
    if (!read_uint8(data, offset, lk)) return false;
    conn.one_way = (ow != 0);
    conn.locked = (lk != 0);

    if (!read_string(data, offset, conn.connector_type)) return false;
    if (!read_uint8(data, offset, am)) return false;
    conn.activation_mode = static_cast<int>(am);

    return true;
}

// --- Mechanism helpers ---

void GameChunkSerializer::write_mechanism(
    std::vector<uint8_t>& buf,
    const MechanismPlacement& mechanism) {
    write_string(buf, mechanism.mechanism_id);
    write_string(buf, mechanism.dimension_id);
    write_int32(buf, mechanism.cell_x);
    write_int32(buf, mechanism.cell_y);
    write_int32(buf, mechanism.cell_z);
    write_string(buf, mechanism.title_key);
    write_string(buf, mechanism.action_label);
    write_string(buf, mechanism.flag_name);
    write_uint8(buf, static_cast<uint8_t>(mechanism.activation_mode));
    write_uint8(buf, mechanism.one_shot ? 1 : 0);
    write_string(buf, mechanism.required_flag);

    write_uint32(buf, static_cast<uint32_t>(mechanism.effects.size()));
    for (const auto& effect : mechanism.effects) {
        write_mechanism_effect(buf, effect);
    }
}

bool GameChunkSerializer::read_mechanism(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MechanismPlacement& mechanism) {
    if (!read_string(data, offset, mechanism.mechanism_id)) return false;
    if (!read_string(data, offset, mechanism.dimension_id)) return false;
    if (!read_int32(data, offset, mechanism.cell_x)) return false;
    if (!read_int32(data, offset, mechanism.cell_y)) return false;
    if (!read_int32(data, offset, mechanism.cell_z)) return false;
    if (!read_string(data, offset, mechanism.title_key)) return false;
    if (!read_string(data, offset, mechanism.action_label)) return false;
    if (!read_string(data, offset, mechanism.flag_name)) return false;

    uint8_t activation_mode;
    uint8_t one_shot;
    if (!read_uint8(data, offset, activation_mode)) return false;
    if (!read_uint8(data, offset, one_shot)) return false;
    mechanism.activation_mode = static_cast<int>(activation_mode);
    mechanism.one_shot = (one_shot != 0);

    if (!read_string(data, offset, mechanism.required_flag)) return false;

    uint32_t effect_count;
    if (!read_uint32(data, offset, effect_count)) return false;
    mechanism.effects.clear();
    mechanism.effects.reserve(effect_count);
    for (uint32_t i = 0; i < effect_count; ++i) {
        MechanismEffectPlacement effect;
        if (!read_mechanism_effect(data, offset, effect)) return false;
        mechanism.effects.push_back(std::move(effect));
    }

    return true;
}

void GameChunkSerializer::write_mechanism_effect(
    std::vector<uint8_t>& buf,
    const MechanismEffectPlacement& effect) {
    write_string(buf, effect.effect_type);
    write_uint64(buf, static_cast<uint64_t>(effect.connector_id));
    write_uint8(buf, effect.when_active ? 1 : 0);
    write_uint8(buf, effect.when_inactive ? 1 : 0);
}

bool GameChunkSerializer::read_mechanism_effect(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MechanismEffectPlacement& effect) {
    if (!read_string(data, offset, effect.effect_type)) return false;

    uint64_t connector_id;
    if (!read_uint64(data, offset, connector_id)) return false;
    effect.connector_id = static_cast<int64_t>(connector_id);

    uint8_t when_active;
    uint8_t when_inactive;
    if (!read_uint8(data, offset, when_active)) return false;
    if (!read_uint8(data, offset, when_inactive)) return false;
    effect.when_active = (when_active != 0);
    effect.when_inactive = (when_inactive != 0);

    return true;
}

// --- Block entity helpers ---

void GameChunkSerializer::write_block_entity(
    std::vector<uint8_t>& buf,
    const BlockEntityPlacement& entity) {
    write_uint64(buf, entity.id.id);
    write_uint8(buf, static_cast<uint8_t>(entity.entity_type));
    write_int32(buf, entity.root_x);
    write_int32(buf, entity.root_y);
    write_int32(buf, entity.root_z);
    write_string(buf, entity.type_data_json);
    write_uint32(buf, entity.owned_cell_count);
}

bool GameChunkSerializer::read_block_entity(
    const std::vector<uint8_t>& data,
    size_t& offset,
    BlockEntityPlacement& entity) {
    uint64_t raw_id;
    if (!read_uint64(data, offset, raw_id)) return false;
    entity.id = EntityId{raw_id};

    uint8_t type_byte;
    if (!read_uint8(data, offset, type_byte)) return false;
    entity.entity_type = static_cast<BlockEntityType>(type_byte);

    if (!read_int32(data, offset, entity.root_x)) return false;
    if (!read_int32(data, offset, entity.root_y)) return false;
    if (!read_int32(data, offset, entity.root_z)) return false;

    if (!read_string(data, offset, entity.type_data_json)) return false;

    if (!read_uint32(data, offset, entity.owned_cell_count)) return false;

    return true;
}

// --- Tree-growth sidecar helpers ---

void GameChunkSerializer::write_tree_growth_record(
    std::vector<uint8_t>& buf,
    const TreeGrowthPersistenceRecord& record) {
    write_uint64(buf, record.anchor_entity_id.id);
    write_string(buf, record.species_key);
    write_uint8(buf, static_cast<uint8_t>(record.growth_stage));
    write_uint64(buf, record.planted_tick);
    write_uint64(buf, record.last_growth_tick);
    write_uint32(buf, static_cast<uint32_t>(record.owned_cells.size()));
    for (const TreeGrowthOwnedCell& cell : record.owned_cells) {
        write_int32(buf, cell.block_x);
        write_int32(buf, cell.block_y);
        write_int32(buf, cell.block_z);
        write_uint32(buf, static_cast<uint32_t>(cell.material));
    }
}

bool GameChunkSerializer::read_tree_growth_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    TreeGrowthPersistenceRecord& record) {
    uint64_t anchor_id = 0;
    uint8_t stage = 0;
    if (!read_uint64(data, offset, anchor_id) || anchor_id == 0 ||
        !read_string(data, offset, record.species_key) ||
        record.species_key.empty() ||
        record.species_key.size() > kMaxTreeGrowthSpeciesKeyBytes ||
        record.species_key.find('\0') != std::string::npos ||
        !read_uint8(data, offset, stage) ||
        stage >= static_cast<uint8_t>(TreeGrowthStage::COUNT) ||
        !read_uint64(data, offset, record.planted_tick) ||
        !read_uint64(data, offset, record.last_growth_tick)) {
        return false;
    }
    record.anchor_entity_id = EntityId{anchor_id};
    record.growth_stage = static_cast<TreeGrowthStage>(stage);

    uint32_t owned_cell_count = 0;
    if (!read_uint32(data, offset, owned_cell_count) ||
        owned_cell_count > kMaxTreeGrowthOwnedCells) {
        return false;
    }
    record.owned_cells.clear();
    record.owned_cells.reserve(owned_cell_count);
    for (uint32_t index = 0; index < owned_cell_count; ++index) {
        TreeGrowthOwnedCell cell;
        uint32_t material = 0;
        if (!read_int32(data, offset, cell.block_x) ||
            !read_int32(data, offset, cell.block_y) ||
            !read_int32(data, offset, cell.block_z) ||
            !read_uint32(data, offset, material)) {
            return false;
        }
        cell.material = static_cast<TerrainMaterialId>(material);
        record.owned_cells.push_back(cell);
    }
    return true;
}

// --- Crop and farmland sidecar helpers ---

void GameChunkSerializer::write_farmland_record(
    std::vector<uint8_t>& buf,
    const FarmlandPersistenceRecord& record) {
    write_uint64(buf, record.anchor_entity_id.id);
    write_float(buf, record.moisture);
    write_float(buf, record.fertility);
    write_string(buf, record.last_crop_key);
    write_uint32(buf, record.consecutive_same_crop);
    write_uint64(buf, record.last_moisture_tick);
}

bool GameChunkSerializer::read_farmland_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    FarmlandPersistenceRecord& record) {
    uint64_t anchor_id = 0;
    if (!read_uint64(data, offset, anchor_id) || anchor_id == 0 ||
        !read_float(data, offset, record.moisture) ||
        !read_float(data, offset, record.fertility) ||
        !std::isfinite(record.moisture) || !std::isfinite(record.fertility) ||
        record.moisture < 0.0f || record.moisture > 1.0f ||
        record.fertility < 0.0f || record.fertility > 1.0f ||
        !read_string(data, offset, record.last_crop_key) ||
        record.last_crop_key.size() > kMaxFarmlandCropKeyBytes ||
        record.last_crop_key.find('\0') != std::string::npos ||
        !read_uint32(data, offset, record.consecutive_same_crop) ||
        !read_uint64(data, offset, record.last_moisture_tick)) {
        return false;
    }
    record.anchor_entity_id = EntityId{anchor_id};
    return true;
}

void GameChunkSerializer::write_crop_growth_record(
    std::vector<uint8_t>& buf,
    const CropGrowthPersistenceRecord& record) {
    write_uint64(buf, record.anchor_entity_id.id);
    write_uint64(buf, record.farmland_anchor_entity_id.id);
    write_string(buf, record.species_key);
    write_uint8(buf, static_cast<uint8_t>(record.growth_stage));
    write_uint64(buf, record.planted_tick);
    write_uint64(buf, record.last_growth_tick);
    write_uint64(buf, record.last_harvest_tick);
    write_uint8(buf, record.is_regrowing ? 1 : 0);
    write_uint32(buf, static_cast<uint32_t>(record.owned_cells.size()));
    for (const CropGrowthOwnedCell& cell : record.owned_cells) {
        write_int32(buf, cell.block_x);
        write_int32(buf, cell.block_y);
        write_int32(buf, cell.block_z);
        write_uint32(buf, static_cast<uint32_t>(cell.material));
    }
}

bool GameChunkSerializer::read_crop_growth_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    CropGrowthPersistenceRecord& record) {
    uint64_t anchor_id = 0;
    uint64_t farmland_anchor_id = 0;
    uint8_t stage = 0;
    uint8_t is_regrowing = 0;
    if (!read_uint64(data, offset, anchor_id) || anchor_id == 0 ||
        !read_uint64(data, offset, farmland_anchor_id) ||
        !read_string(data, offset, record.species_key) ||
        record.species_key.empty() ||
        record.species_key.size() > kMaxCropGrowthSpeciesKeyBytes ||
        record.species_key.find('\0') != std::string::npos ||
        !read_uint8(data, offset, stage) ||
        stage >= static_cast<uint8_t>(CropGrowthStage::COUNT) ||
        !read_uint64(data, offset, record.planted_tick) ||
        !read_uint64(data, offset, record.last_growth_tick) ||
        !read_uint64(data, offset, record.last_harvest_tick) ||
        !read_uint8(data, offset, is_regrowing) || is_regrowing > 1) {
        return false;
    }
    record.anchor_entity_id = EntityId{anchor_id};
    record.farmland_anchor_entity_id = EntityId{farmland_anchor_id};
    record.growth_stage = static_cast<CropGrowthStage>(stage);
    record.is_regrowing = is_regrowing != 0;

    uint32_t owned_cell_count = 0;
    if (!read_uint32(data, offset, owned_cell_count) ||
        owned_cell_count == 0 || owned_cell_count > kMaxCropGrowthOwnedCells) {
        return false;
    }
    record.owned_cells.clear();
    record.owned_cells.reserve(owned_cell_count);
    for (uint32_t index = 0; index < owned_cell_count; ++index) {
        CropGrowthOwnedCell cell;
        uint32_t material = 0;
        if (!read_int32(data, offset, cell.block_x) ||
            !read_int32(data, offset, cell.block_y) ||
            !read_int32(data, offset, cell.block_z) ||
            !read_uint32(data, offset, material)) {
            return false;
        }
        cell.material = static_cast<TerrainMaterialId>(material);
        record.owned_cells.push_back(cell);
    }
    return true;
}

// --- Player bed/grave sidecar helpers ---

void GameChunkSerializer::write_player_bed_record(
    std::vector<uint8_t>& buf,
    const GamePlayerBedRecord& record) {
    write_int32(buf, record.root_x);
    write_int32(buf, record.root_y);
    write_int32(buf, record.root_z);
}

bool GameChunkSerializer::read_player_bed_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    GamePlayerBedRecord& record) {
    return read_int32(data, offset, record.root_x) &&
           read_int32(data, offset, record.root_y) &&
           read_int32(data, offset, record.root_z);
}

void GameChunkSerializer::write_player_grave_item_stack(
    std::vector<uint8_t>& buf,
    const GamePlayerGraveItemStack& stack) {
    write_string(buf, stack.resource.key.type);
    write_string(buf, stack.resource.key.id);
    write_string(buf, stack.resource.key.variant);
    write_int64(buf, stack.resource.amount);
    write_string(buf, stack.instance_data);
}

bool GameChunkSerializer::read_player_grave_item_stack(
    const std::vector<uint8_t>& data,
    size_t& offset,
    GamePlayerGraveItemStack& stack) {
    return read_string(data, offset, stack.resource.key.type) &&
           read_string(data, offset, stack.resource.key.id) &&
           read_string(data, offset, stack.resource.key.variant) &&
           read_int64(data, offset, stack.resource.amount) &&
           read_string(data, offset, stack.instance_data);
}

void GameChunkSerializer::write_player_grave_record(
    std::vector<uint8_t>& buf,
    const GamePlayerGraveRecord& record) {
    write_uint64(buf, record.grave_id);
    write_string(buf, record.owner_account_id);
    write_uint64(buf, record.death_tick);
    write_int32(buf, record.root_x);
    write_int32(buf, record.root_y);
    write_int32(buf, record.root_z);
    write_uint32(buf, static_cast<uint32_t>(record.items.size()));
    for (const GamePlayerGraveItemStack& item : record.items) {
        write_player_grave_item_stack(buf, item);
    }
}

bool GameChunkSerializer::read_player_grave_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    GamePlayerGraveRecord& record) {
    if (!read_uint64(data, offset, record.grave_id) ||
        !read_string(data, offset, record.owner_account_id) ||
        !read_uint64(data, offset, record.death_tick) ||
        !read_int32(data, offset, record.root_x) ||
        !read_int32(data, offset, record.root_y) ||
        !read_int32(data, offset, record.root_z)) {
        return false;
    }
    uint32_t item_count;
    if (!read_uint32(data, offset, item_count) || item_count == 0 ||
        item_count > kMaxPlayerGraveItemStacks) {
        return false;
    }
    record.items.clear();
    record.items.reserve(item_count);
    for (uint32_t index = 0; index < item_count; ++index) {
        GamePlayerGraveItemStack item;
        if (!read_player_grave_item_stack(data, offset, item) ||
            !item.resource.is_valid() || !item.resource.is_item() ||
            (!item.instance_data.empty() && item.resource.amount != 1)) {
            return false;
        }
        record.items.push_back(std::move(item));
    }
    return record.grave_id != 0 && !record.owner_account_id.empty();
}

// --- Machine runtime sidecar helpers ---

void GameChunkSerializer::write_machine_runtime_item_stack(
    std::vector<uint8_t>& buf,
    const MachineRuntimeItemStack& stack) {
    write_string(buf, stack.resource.key.type);
    write_string(buf, stack.resource.key.id);
    write_string(buf, stack.resource.key.variant);
    write_int64(buf, stack.resource.amount);
}

bool GameChunkSerializer::read_machine_runtime_item_stack(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MachineRuntimeItemStack& stack) {
    return read_string(data, offset, stack.resource.key.type) &&
           read_string(data, offset, stack.resource.key.id) &&
           read_string(data, offset, stack.resource.key.variant) &&
           read_int64(data, offset, stack.resource.amount);
}

void GameChunkSerializer::write_machine_runtime_recipe_snapshot(
    std::vector<uint8_t>& buf,
    const MachineRuntimeRecipeSnapshot& recipe) {
    write_string(buf, recipe.id);
    write_uint32(buf, static_cast<uint32_t>(recipe.inputs.size()));
    for (const MachineRuntimeItemStack& input : recipe.inputs) {
        write_machine_runtime_item_stack(buf, input);
    }
    write_uint32(buf, static_cast<uint32_t>(recipe.outputs.size()));
    for (const MachineRuntimeItemStack& output : recipe.outputs) {
        write_machine_runtime_item_stack(buf, output);
    }
    write_int32(buf, recipe.duration_ticks);
    write_int32(buf, recipe.energy_per_tick);
}

bool GameChunkSerializer::read_machine_runtime_recipe_snapshot(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MachineRuntimeRecipeSnapshot& recipe) {
    if (!read_string(data, offset, recipe.id)) {
        return false;
    }

    uint32_t input_count;
    if (!read_uint32(data, offset, input_count) ||
        input_count > kMaxMachineRecipeInputs) {
        return false;
    }
    recipe.inputs.clear();
    recipe.inputs.reserve(input_count);
    for (uint32_t index = 0; index < input_count; ++index) {
        MachineRuntimeItemStack input;
        if (!read_machine_runtime_item_stack(data, offset, input)) return false;
        recipe.inputs.push_back(std::move(input));
    }

    uint32_t output_count;
    if (!read_uint32(data, offset, output_count) ||
        output_count > kMaxMachineRecipeOutputs) {
        return false;
    }
    recipe.outputs.clear();
    recipe.outputs.reserve(output_count);
    for (uint32_t index = 0; index < output_count; ++index) {
        MachineRuntimeItemStack output;
        if (!read_machine_runtime_item_stack(data, offset, output)) return false;
        recipe.outputs.push_back(std::move(output));
    }

    return read_int32(data, offset, recipe.duration_ticks) &&
           read_int32(data, offset, recipe.energy_per_tick);
}

void GameChunkSerializer::write_machine_runtime_record(
    std::vector<uint8_t>& buf,
    const MachineRuntimePersistenceRecord& record) {
    write_uint64(buf, record.anchor_entity_id.id);
    write_uint64(buf, record.entity_guid);
    write_string(buf, record.machine_id);
    write_uint32(buf, static_cast<uint32_t>(record.input_slots.size()));
    for (const MachineRuntimeItemStack& input : record.input_slots) {
        write_machine_runtime_item_stack(buf, input);
    }
    write_uint32(buf, static_cast<uint32_t>(record.output_slots.size()));
    for (const MachineRuntimeItemStack& output : record.output_slots) {
        write_machine_runtime_item_stack(buf, output);
    }
    write_int32(buf, record.stored_energy);
    write_int32(buf, record.energy_capacity);
    write_int32(buf, record.max_input_slots);
    write_int32(buf, record.max_output_slots);
    write_int32(buf, record.max_stack_size);
    write_int32(buf, record.progress_ticks);
    write_uint8(buf, record.active_recipe.has_value() ? 1 : 0);
    if (record.active_recipe) {
        write_machine_runtime_recipe_snapshot(buf, *record.active_recipe);
    }
    write_uint8(buf, record.activation_requested ? 1 : 0);
    write_string(buf, record.job_owner_account_id);
    write_uint8(buf, record.run_state);
    write_uint8(buf, static_cast<uint8_t>(record.residency));
    write_uint64(buf, record.offline_last_simulated_tick);
    write_uint64(buf, record.offline_island_id);
    write_uint64(buf, record.offline_epoch);
}

bool GameChunkSerializer::read_machine_runtime_record(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MachineRuntimePersistenceRecord& record) {
    uint64_t anchor_id;
    if (!read_uint64(data, offset, anchor_id) ||
        !read_uint64(data, offset, record.entity_guid) ||
        !read_string(data, offset, record.machine_id)) {
        return false;
    }
    record.anchor_entity_id = EntityId{anchor_id};

    uint32_t input_count;
    if (!read_uint32(data, offset, input_count) ||
        input_count > kMaxMachineInputSlots) {
        return false;
    }
    record.input_slots.clear();
    record.input_slots.reserve(input_count);
    for (uint32_t index = 0; index < input_count; ++index) {
        MachineRuntimeItemStack input;
        if (!read_machine_runtime_item_stack(data, offset, input)) return false;
        record.input_slots.push_back(std::move(input));
    }

    uint32_t output_count;
    if (!read_uint32(data, offset, output_count) ||
        output_count > kMaxMachineOutputSlots) {
        return false;
    }
    record.output_slots.clear();
    record.output_slots.reserve(output_count);
    for (uint32_t index = 0; index < output_count; ++index) {
        MachineRuntimeItemStack output;
        if (!read_machine_runtime_item_stack(data, offset, output)) return false;
        record.output_slots.push_back(std::move(output));
    }

    if (!read_int32(data, offset, record.stored_energy) ||
        !read_int32(data, offset, record.energy_capacity) ||
        !read_int32(data, offset, record.max_input_slots) ||
        !read_int32(data, offset, record.max_output_slots) ||
        !read_int32(data, offset, record.max_stack_size) ||
        !read_int32(data, offset, record.progress_ticks)) {
        return false;
    }

    uint8_t has_active_recipe;
    if (!read_uint8(data, offset, has_active_recipe) || has_active_recipe > 1) {
        return false;
    }
    record.active_recipe.reset();
    if (has_active_recipe != 0) {
        MachineRuntimeRecipeSnapshot recipe;
        if (!read_machine_runtime_recipe_snapshot(data, offset, recipe)) return false;
        record.active_recipe = std::move(recipe);
    }

    uint8_t activation_requested;
    if (!read_uint8(data, offset, activation_requested) || activation_requested > 1) {
        return false;
    }
    record.activation_requested = activation_requested != 0;

    if (!read_string(data, offset, record.job_owner_account_id) ||
        record.job_owner_account_id.size() > kMaxMachineJobOwnerAccountBytes ||
        record.job_owner_account_id.find('\0') != std::string::npos) {
        return false;
    }

    if (!read_uint8(data, offset, record.run_state) ||
        record.run_state >= kMachineRunStateCount) {
        return false;
    }
    uint8_t residency;
    if (!read_uint8(data, offset, residency) ||
        residency > static_cast<uint8_t>(MachineRuntimeResidency::kOfflineNetworkIsland) ||
        !read_uint64(data, offset, record.offline_last_simulated_tick) ||
        !read_uint64(data, offset, record.offline_island_id) ||
        !read_uint64(data, offset, record.offline_epoch)) {
        return false;
    }
    record.residency = static_cast<MachineRuntimeResidency>(residency);
    return true;
}

// --- Offline network-island sidecar helpers ---

void GameChunkSerializer::write_chunk_key(std::vector<uint8_t>& buf,
                                          const ChunkKey& key) {
    write_string(buf, key.dimension_id);
    write_int32(buf, key.chunk_x);
    write_int32(buf, key.chunk_y);
    write_int32(buf, key.chunk_z);
}

bool GameChunkSerializer::read_chunk_key(const std::vector<uint8_t>& data,
                                         size_t& offset,
                                         ChunkKey& key) {
    return read_string(data, offset, key.dimension_id) &&
           key.dimension_id.size() <= kMaxOfflineNetworkDimensionIdBytes &&
           !key.dimension_id.empty() &&
           key.dimension_id.find('\0') == std::string::npos &&
           read_int32(data, offset, key.chunk_x) &&
           read_int32(data, offset, key.chunk_y) &&
           read_int32(data, offset, key.chunk_z);
}

void GameChunkSerializer::write_offline_network_island_snapshot(
    std::vector<uint8_t>& buf,
    const OfflineNetworkIslandSnapshot& snapshot) {
    write_uint64(buf, snapshot.island_id);
    write_uint64(buf, snapshot.ownership_epoch);
    write_string(buf, snapshot.dimension_id);
    write_chunk_key(buf, snapshot.anchor_chunk);
    write_uint32(buf, static_cast<uint32_t>(snapshot.member_chunks.size()));
    for (const ChunkKey& chunk_key : snapshot.member_chunks) {
        write_chunk_key(buf, chunk_key);
    }
    write_uint64(buf, snapshot.topology_revision);
    write_uint64(buf, snapshot.last_simulated_tick);
    write_uint32(buf, static_cast<uint32_t>(snapshot.machine_guids.size()));
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        write_uint64(buf, entity_guid);
    }
    write_uint32(buf, static_cast<uint32_t>(snapshot.boundary_ports.size()));
    for (const OfflineNetworkBoundaryPort& port : snapshot.boundary_ports) {
        write_uint64(buf, port.node_id);
        write_chunk_key(buf, port.adjacent_chunk);
        write_uint8(buf, port.direction);
        write_uint64(buf, port.topology_revision);
    }
    write_uint32(buf, static_cast<uint32_t>(snapshot.ledgers.size()));
    for (const OfflineNetworkResourceLedger& ledger : snapshot.ledgers) {
        write_uint8(buf, static_cast<uint8_t>(ledger.kind));
        write_string(buf, ledger.resource_id);
        write_int64(buf, ledger.stored_amount);
        write_int64(buf, ledger.capacity);
    }
}

bool GameChunkSerializer::read_offline_network_island_snapshot(
    const std::vector<uint8_t>& data,
    size_t& offset,
    OfflineNetworkIslandSnapshot& snapshot) {
    if (!read_uint64(data, offset, snapshot.island_id) || snapshot.island_id == 0 ||
        !read_uint64(data, offset, snapshot.ownership_epoch) ||
        snapshot.ownership_epoch == 0 ||
        !read_string(data, offset, snapshot.dimension_id) ||
        snapshot.dimension_id.empty() ||
        snapshot.dimension_id.size() > kMaxOfflineNetworkDimensionIdBytes ||
        snapshot.dimension_id.find('\0') != std::string::npos ||
        !read_chunk_key(data, offset, snapshot.anchor_chunk) ||
        snapshot.anchor_chunk.dimension_id != snapshot.dimension_id) {
        return false;
    }

    uint32_t member_chunk_count = 0;
    if (!read_uint32(data, offset, member_chunk_count) || member_chunk_count == 0 ||
        member_chunk_count > kMaxOfflineNetworkMemberChunks) {
        return false;
    }
    snapshot.member_chunks.clear();
    snapshot.member_chunks.reserve(member_chunk_count);
    for (uint32_t index = 0; index < member_chunk_count; ++index) {
        ChunkKey member_chunk;
        if (!read_chunk_key(data, offset, member_chunk) ||
            member_chunk.dimension_id != snapshot.dimension_id) {
            return false;
        }
        snapshot.member_chunks.push_back(std::move(member_chunk));
    }

    if (!read_uint64(data, offset, snapshot.topology_revision) ||
        !read_uint64(data, offset, snapshot.last_simulated_tick)) {
        return false;
    }

    uint32_t machine_guid_count = 0;
    if (!read_uint32(data, offset, machine_guid_count) || machine_guid_count == 0 ||
        machine_guid_count > kMaxOfflineNetworkMachineGuids) {
        return false;
    }
    snapshot.machine_guids.clear();
    snapshot.machine_guids.reserve(machine_guid_count);
    for (uint32_t index = 0; index < machine_guid_count; ++index) {
        uint64_t entity_guid = 0;
        if (!read_uint64(data, offset, entity_guid) || entity_guid == 0) return false;
        snapshot.machine_guids.push_back(entity_guid);
    }

    uint32_t boundary_port_count = 0;
    if (!read_uint32(data, offset, boundary_port_count) ||
        boundary_port_count > kMaxOfflineNetworkBoundaryPorts) {
        return false;
    }
    snapshot.boundary_ports.clear();
    snapshot.boundary_ports.reserve(boundary_port_count);
    for (uint32_t index = 0; index < boundary_port_count; ++index) {
        OfflineNetworkBoundaryPort port;
        if (!read_uint64(data, offset, port.node_id) || port.node_id == 0 ||
            !read_chunk_key(data, offset, port.adjacent_chunk) ||
            port.adjacent_chunk.dimension_id != snapshot.dimension_id ||
            !read_uint8(data, offset, port.direction) || port.direction >= 6 ||
            !read_uint64(data, offset, port.topology_revision)) {
            return false;
        }
        snapshot.boundary_ports.push_back(std::move(port));
    }

    uint32_t ledger_count = 0;
    if (!read_uint32(data, offset, ledger_count) ||
        ledger_count > kMaxOfflineNetworkLedgers) {
        return false;
    }
    snapshot.ledgers.clear();
    snapshot.ledgers.reserve(ledger_count);
    for (uint32_t index = 0; index < ledger_count; ++index) {
        uint8_t raw_kind = 0;
        OfflineNetworkResourceLedger ledger;
        if (!read_uint8(data, offset, raw_kind) ||
            raw_kind > static_cast<uint8_t>(OfflineNetworkResourceKind::kFluid) ||
            !read_string(data, offset, ledger.resource_id) ||
            ledger.resource_id.empty() ||
            ledger.resource_id.size() > kMaxOfflineNetworkResourceIdBytes ||
            ledger.resource_id.find('\0') != std::string::npos ||
            !read_int64(data, offset, ledger.stored_amount) ||
            !read_int64(data, offset, ledger.capacity) ||
            ledger.stored_amount < 0 || ledger.capacity < 0 ||
            ledger.stored_amount > ledger.capacity) {
            return false;
        }
        ledger.kind = static_cast<OfflineNetworkResourceKind>(raw_kind);
        snapshot.ledgers.push_back(std::move(ledger));
    }
    return true;
}

// --- Population cell helpers ---

void GameChunkSerializer::write_population_cell(
    std::vector<uint8_t>& buf,
    const PopulationCell& cell) {
    write_float(buf, cell.vegetation_density);
    write_float(buf, cell.herbivore_density);
    write_float(buf, cell.predator_density);
    write_float(buf, cell.soil_fertility);
    write_float(buf, cell.water_availability);
    write_float(buf, cell.dead_biomass);
    write_uint8(buf, cell.biome_type);
    write_float(buf, cell.hunting_pressure_herb);
    write_float(buf, cell.hunting_pressure_pred);
    write_uint64(buf, cell.last_macro_simulation_tick);
}

bool GameChunkSerializer::read_population_cell(
    const std::vector<uint8_t>& data,
    size_t& offset,
    PopulationCell& cell) {
    if (!read_float(data, offset, cell.vegetation_density)) return false;
    if (!read_float(data, offset, cell.herbivore_density)) return false;
    if (!read_float(data, offset, cell.predator_density)) return false;
    if (!read_float(data, offset, cell.soil_fertility)) return false;
    if (!read_float(data, offset, cell.water_availability)) return false;
    if (!read_float(data, offset, cell.dead_biomass)) return false;
    uint8_t biome;
    if (!read_uint8(data, offset, biome)) return false;
    cell.biome_type = biome;
    if (!read_float(data, offset, cell.hunting_pressure_herb)) return false;
    return read_float(data, offset, cell.hunting_pressure_pred) &&
           read_uint64(data, offset, cell.last_macro_simulation_tick);
}

// --- Captive creature helpers ---

void GameChunkSerializer::write_captive_creature(
    std::vector<uint8_t>& buf,
    const CaptiveCreature& cc) const {
    // Runtime IDs are remapped through stable species keys.
    std::string species_key;
    std::string partner_key;
    const auto& registry = species_catalog();
    const CreatureSpeciesDef* def = registry.get_species(cc.species_id);
    if (def) species_key = def->species_key;
    const CreatureSpeciesDef* partner_def = registry.get_species(cc.partner_species_id);
    if (partner_def) partner_key = partner_def->species_key;

    write_string(buf, species_key);
    write_uint8(buf, static_cast<uint8_t>(cc.role));
    write_uint8(buf, static_cast<uint8_t>(cc.age_stage));
    write_float(buf, cc.pos_x);
    write_float(buf, cc.pos_y);
    write_float(buf, cc.pos_z);
    write_float(buf, cc.wander_target_x);
    write_float(buf, cc.wander_target_y);
    write_float(buf, cc.wander_target_z);
    write_int64(buf, cc.next_wander_tick);
    write_int32(buf, cc.bounds_min_x);
    write_int32(buf, cc.bounds_min_y);
    write_int32(buf, cc.bounds_min_z);
    write_int32(buf, cc.bounds_max_x);
    write_int32(buf, cc.bounds_max_y);
    write_int32(buf, cc.bounds_max_z);
    write_float(buf, cc.health);
    write_float(buf, cc.tame_progress);
    write_uint8(buf, cc.is_tamed ? 1 : 0);
    write_int64(buf, cc.capture_tick);
    write_int64(buf, cc.birth_tick);
    write_int64(buf, cc.grow_up_tick);
    write_int64(buf, cc.breed_cooldown_until);
    write_int64(buf, cc.gestation_end_tick);
    write_uint8(buf, cc.is_pregnant ? 1 : 0);
    write_string(buf, partner_key);
}

bool GameChunkSerializer::read_captive_creature(
    const std::vector<uint8_t>& data,
    size_t& offset,
    CaptiveCreature& cc) const {
    std::string species_key;
    if (!read_string(data, offset, species_key)) return false;
    if (species_key.empty()) {
        cc.species_id = 0;
    } else {
        const CreatureSpeciesDef* def =
            species_catalog().get_species_by_key(species_key);
        cc.species_id = def ? def->species_id : 0;
    }

    uint8_t role;
    if (!read_uint8(data, offset, role)) return false;
    cc.role = static_cast<CreatureRole>(role);
    uint8_t age;
    if (!read_uint8(data, offset, age)) return false;
    cc.age_stage = static_cast<CreatureAgeStage>(age);
    if (!read_float(data, offset, cc.pos_x)) return false;
    if (!read_float(data, offset, cc.pos_y)) return false;
    if (!read_float(data, offset, cc.pos_z)) return false;
    if (!read_float(data, offset, cc.wander_target_x)) return false;
    if (!read_float(data, offset, cc.wander_target_y)) return false;
    if (!read_float(data, offset, cc.wander_target_z)) return false;
    if (!read_int64(data, offset, cc.next_wander_tick)) return false;
    if (!read_int32(data, offset, cc.bounds_min_x)) return false;
    if (!read_int32(data, offset, cc.bounds_min_y)) return false;
    if (!read_int32(data, offset, cc.bounds_min_z)) return false;
    if (!read_int32(data, offset, cc.bounds_max_x)) return false;
    if (!read_int32(data, offset, cc.bounds_max_y)) return false;
    if (!read_int32(data, offset, cc.bounds_max_z)) return false;
    if (!read_float(data, offset, cc.health)) return false;
    if (!read_float(data, offset, cc.tame_progress)) return false;
    uint8_t tamed;
    if (!read_uint8(data, offset, tamed)) return false;
    cc.is_tamed = (tamed != 0);
    if (!read_int64(data, offset, cc.capture_tick)) return false;
    if (!read_int64(data, offset, cc.birth_tick)) return false;
    if (!read_int64(data, offset, cc.grow_up_tick)) return false;
    if (!read_int64(data, offset, cc.breed_cooldown_until)) return false;
    if (!read_int64(data, offset, cc.gestation_end_tick)) return false;
    uint8_t pregnant;
    if (!read_uint8(data, offset, pregnant)) return false;
    cc.is_pregnant = (pregnant != 0);

    std::string partner_key;
    if (!read_string(data, offset, partner_key)) return false;
    if (partner_key.empty()) {
        cc.partner_species_id = 0;
    } else {
        const CreatureSpeciesDef* partner_def =
            species_catalog().get_species_by_key(partner_key);
        cc.partner_species_id = partner_def ? partner_def->species_id : 0;
    }
    return true;
}

} // namespace snt::game
