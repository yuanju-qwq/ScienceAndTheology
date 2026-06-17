#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include "entity_data.hpp"
#include "block_entity.hpp"

namespace science_and_theology {

// ============================================================
// BlockEntityRegistry — global registry for all block entities
// ============================================================
//
// Provides:
//   - O(1) lookup by EntityId
//   - Spatial lookup by world position (which entity owns a cell?)
//   - Per-chunk iteration (entities whose root is in a given chunk)
//
// Thread safety: main thread only. Not safe for concurrent access.
// The registry is owned by WorldData and accessed by simulation systems.

class BlockEntityRegistry {
public:
    // Internal storage for a block entity with its runtime state.
    // Defined first so that all_entities() can reference it.
    struct BlockEntityEntry {
        BlockEntityPlacement placement;
        std::string dimension_id;

        // Type-specific runtime state.
        // Only one variant is active at a time, determined by entity_type.
        TreeBlockEntityState tree_state;
    };

    BlockEntityRegistry() = default;
    ~BlockEntityRegistry() = default;

    // Disallow copy.
    BlockEntityRegistry(const BlockEntityRegistry&) = delete;
    BlockEntityRegistry& operator=(const BlockEntityRegistry&) = delete;

    // --- Entity ID generation ---

    // Generate the next unique EntityId for a block entity.
    EntityId next_id();

    // --- Registration ---

    // Register a tree block entity. Returns its assigned EntityId.
    EntityId register_tree_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        const std::string& species_key,
        TreeGrowthStage growth_stage,
        int64_t planted_tick,
        const std::vector<OwnedCell>& owned_cells);

    // Remove a block entity by ID. Also removes its spatial index entries.
    void remove_entity(EntityId id);

    // Remove all entities whose root is in the given chunk.
    void remove_entities_in_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z);

    // --- Query by EntityId ---

    // Returns the BlockEntityType for a given entity, or NONE if not found.
    BlockEntityType get_entity_type(EntityId id) const;

    // Returns the tree state for a given entity, or nullptr if not a tree.
    const TreeBlockEntityState* get_tree_state(EntityId id) const;
    TreeBlockEntityState* get_tree_state_mut(EntityId id);

    // Returns the placement data for a given entity.
    const BlockEntityPlacement* get_placement(EntityId id) const;

    // --- Spatial query ---

    // Returns the EntityId that owns the cell at (block_x, block_y, block_z),
    // or invalid EntityId if no entity owns that cell.
    EntityId find_owner_at(int32_t block_x, int32_t block_y, int32_t block_z) const;

    // Returns all entity IDs whose root is in the given chunk.
    std::vector<EntityId> entities_in_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const;

    // Returns all entity entries in the registry.
    const std::unordered_map<EntityId, BlockEntityEntry>& all_entities() const {
        return entities_;
    }

    // --- Owned cell management ---

    // Update the owned cells for a tree entity.
    // Removes old spatial index entries and adds new ones.
    void update_tree_owned_cells(
        EntityId id, const std::vector<OwnedCell>& new_cells);

    // --- Iteration ---

    // Iterate over all tree entities. Callback receives (EntityId, TreeBlockEntityState).
    void for_each_tree(
        std::function<void(EntityId, const TreeBlockEntityState&)> fn) const;

    // Returns the total number of registered entities.
    size_t size() const { return entities_.size(); }

    // Remove all entities and reset the ID counter.
    void clear();

private:
    // Spatial index key for a cell position.
    struct CellKey {
        int32_t x, y, z;

        bool operator==(const CellKey& o) const {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct CellKeyHash {
        size_t operator()(const CellKey& k) const {
            size_t h = static_cast<size_t>(k.x) * 73856093ULL;
            h ^= static_cast<size_t>(k.y) * 19349663ULL;
            h ^= static_cast<size_t>(k.z) * 83492791ULL;
            return h;
        }
    };

    // Chunk key for per-chunk entity grouping.
    struct ChunkRefKey {
        std::string dimension_id;
        int chunk_x, chunk_y, chunk_z;

        bool operator==(const ChunkRefKey& o) const {
            return dimension_id == o.dimension_id
                && chunk_x == o.chunk_x
                && chunk_y == o.chunk_y
                && chunk_z == o.chunk_z;
        }
    };

    struct ChunkRefKeyHash {
        size_t operator()(const ChunkRefKey& k) const {
            size_t h = std::hash<std::string>()(k.dimension_id);
            h ^= static_cast<size_t>(k.chunk_x) * 73856093ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(k.chunk_y) * 19349663ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(k.chunk_z) * 83492791ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Add spatial index entries for a set of owned cells.
    void index_owned_cells(EntityId id, const std::vector<OwnedCell>& cells);

    // Remove spatial index entries for a set of owned cells.
    void unindex_owned_cells(const std::vector<OwnedCell>& cells);

    // Compute chunk coordinates from a world block position.
    static ChunkRefKey chunk_for_block(
        const std::string& dimension_id,
        int32_t block_x, int32_t block_y, int32_t block_z);

    // Entity storage: EntityId → entry.
    std::unordered_map<EntityId, BlockEntityEntry> entities_;

    // Spatial index: cell position → owning EntityId.
    std::unordered_map<CellKey, EntityId, CellKeyHash> cell_owners_;

    // Per-chunk index: chunk key → entity IDs rooted in that chunk.
    std::unordered_map<ChunkRefKey, std::vector<EntityId>, ChunkRefKeyHash> chunk_entities_;

    // Monotonic ID counter.
    uint64_t next_id_ = 1;
};

} // namespace science_and_theology
