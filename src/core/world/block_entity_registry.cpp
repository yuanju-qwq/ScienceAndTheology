#include "block_entity_registry.hpp"

#include <algorithm>
#include <cmath>

namespace science_and_theology {

// --- Entity ID generation ---

EntityId BlockEntityRegistry::next_id() {
    EntityId id{next_id_};
    ++next_id_;
    return id;
}

// --- Registration ---

EntityId BlockEntityRegistry::register_tree_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    const std::string& species_key,
    TreeGrowthStage growth_stage,
    int64_t planted_tick,
    const std::vector<OwnedCell>& owned_cells) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::TREE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count =
        static_cast<uint32_t>(owned_cells.size());

    // Encode type-specific data as a simple structured string.
    // Format: "species_key|growth_stage|planted_tick"
    entry.placement.type_data_json =
        species_key + "|" +
        std::to_string(static_cast<int>(growth_stage)) + "|" +
        std::to_string(planted_tick);

    entry.tree_state.species_key = species_key;
    entry.tree_state.growth_stage = growth_stage;
    entry.tree_state.planted_tick = planted_tick;
    entry.tree_state.last_growth_tick = planted_tick;
    entry.tree_state.owned_cells = owned_cells;

    // Index the owned cells for spatial lookup.
    index_owned_cells(id, owned_cells);

    // Index by chunk.
    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

void BlockEntityRegistry::remove_entity(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;

    BlockEntityEntry& entry = it->second;

    // Remove spatial index entries.
    if (entry.placement.entity_type == BlockEntityType::TREE) {
        unindex_owned_cells(entry.tree_state.owned_cells);
    }

    // Remove from chunk index.
    ChunkRefKey ck = chunk_for_block(
        entry.dimension_id,
        entry.placement.root_x,
        entry.placement.root_y,
        entry.placement.root_z);
    auto chunk_it = chunk_entities_.find(ck);
    if (chunk_it != chunk_entities_.end()) {
        auto& ids = chunk_it->second;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty()) {
            chunk_entities_.erase(chunk_it);
        }
    }

    entities_.erase(it);
}

void BlockEntityRegistry::remove_entities_in_chunk(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z) {
    ChunkRefKey ck{dimension_id, chunk_x, chunk_y, chunk_z};
    auto it = chunk_entities_.find(ck);
    if (it == chunk_entities_.end()) return;

    // Copy the IDs since remove_entity modifies chunk_entities_.
    auto ids = it->second;
    for (const auto& id : ids) {
        remove_entity(id);
    }
}

// --- Query by EntityId ---

BlockEntityType BlockEntityRegistry::get_entity_type(EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return BlockEntityType::NONE;
    return it->second.placement.entity_type;
}

const TreeBlockEntityState* BlockEntityRegistry::get_tree_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return nullptr;
    return &it->second.tree_state;
}

TreeBlockEntityState* BlockEntityRegistry::get_tree_state_mut(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return nullptr;
    return &it->second.tree_state;
}

const BlockEntityPlacement* BlockEntityRegistry::get_placement(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    return &it->second.placement;
}

// --- Spatial query ---

EntityId BlockEntityRegistry::find_owner_at(
    int32_t block_x, int32_t block_y, int32_t block_z) const {
    CellKey key{block_x, block_y, block_z};
    auto it = cell_owners_.find(key);
    if (it == cell_owners_.end()) return EntityId{};
    return it->second;
}

std::vector<EntityId> BlockEntityRegistry::entities_in_chunk(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z) const {
    ChunkRefKey ck{dimension_id, chunk_x, chunk_y, chunk_z};
    auto it = chunk_entities_.find(ck);
    if (it == chunk_entities_.end()) return {};
    return it->second;
}

// --- Owned cell management ---

void BlockEntityRegistry::update_tree_owned_cells(
    EntityId id, const std::vector<OwnedCell>& new_cells) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return;

    // Remove old spatial index entries.
    unindex_owned_cells(it->second.tree_state.owned_cells);

    // Update state.
    it->second.tree_state.owned_cells = new_cells;
    it->second.placement.owned_cell_count =
        static_cast<uint32_t>(new_cells.size());

    // Add new spatial index entries.
    index_owned_cells(id, new_cells);
}

// --- Iteration ---

void BlockEntityRegistry::for_each_tree(
    std::function<void(EntityId, const TreeBlockEntityState&)> fn) const {
    for (const auto& pair : entities_) {
        if (pair.second.placement.entity_type == BlockEntityType::TREE) {
            fn(pair.first, pair.second.tree_state);
        }
    }
}

// --- Bulk operations ---

void BlockEntityRegistry::clear() {
    entities_.clear();
    cell_owners_.clear();
    chunk_entities_.clear();
    next_id_ = 1;
}

// --- Private helpers ---

void BlockEntityRegistry::index_owned_cells(
    EntityId id, const std::vector<OwnedCell>& cells) {
    for (const auto& cell : cells) {
        CellKey key{cell.block_x, cell.block_y, cell.block_z};
        cell_owners_[key] = id;
    }
}

void BlockEntityRegistry::unindex_owned_cells(
    const std::vector<OwnedCell>& cells) {
    for (const auto& cell : cells) {
        CellKey key{cell.block_x, cell.block_y, cell.block_z};
        cell_owners_.erase(key);
    }
}

BlockEntityRegistry::ChunkRefKey BlockEntityRegistry::chunk_for_block(
    const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    // Chunk size must match ChunkData::kChunkSize (32).
    constexpr int kChunkSize = 32;
    return ChunkRefKey{
        dimension_id,
        static_cast<int>(std::floor(static_cast<float>(block_x) / kChunkSize)),
        static_cast<int>(std::floor(static_cast<float>(block_y) / kChunkSize)),
        static_cast<int>(std::floor(static_cast<float>(block_z) / kChunkSize)),
    };
}

} // namespace science_and_theology
