#pragma once

#include <memory>
#include <queue>
#include <unordered_map>

#include "chunk_data.hpp"
#include "gameplay_config.hpp"
#include "block_entity_registry.hpp"
#include "machine_collision_overlay.hpp"
#include "../mobile_structure/dynamic_structure.hpp"
#include "../world_gen/world_gen_config.hpp"

namespace science_and_theology {

// A block physics event enqueued after a block is mined.
// BlockPhysicsSystem consumes these each tick.
struct BlockPhysicsEvent {
    std::string dimension_id;
    int block_x = 0;
    int block_y = 0;
    int block_z = 0;
};

// Single source of truth for all world data.
// Manages chunks across all dimensions. Godot nodes act only as rendering proxies.
//
// Usage:
//   WorldData world;
//   ChunkData chunk;
//   chunk.chunk_x = 0;
//   chunk.chunk_y = 0;
//   chunk.chunk_z = 0;
//   world.set_chunk("overworld", 0, 0, 0, chunk);
//   if (auto* c = world.get_chunk("overworld", 0, 0, 0)) { ... }
class WorldData {
public:
    WorldData() = default;
    ~WorldData() = default;

    // Disallow copy, allow move.
    WorldData(const WorldData&) = delete;
    WorldData& operator=(const WorldData&) = delete;
    WorldData(WorldData&&) = default;
    WorldData& operator=(WorldData&&) = default;

    // --- Chunk access ---

    bool has_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z) const;

    ChunkData* get_chunk(const std::string& dimension_id,
                         int chunk_x, int chunk_y, int chunk_z);
    const ChunkData* get_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const;

    // Sets or replaces a chunk. Takes ownership.
    void set_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z, ChunkData chunk);

    // Removes a chunk. Does nothing if the chunk does not exist.
    void remove_chunk(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z);

    // Returns all chunk keys for iterating over the world.
    std::vector<ChunkKey> all_chunk_keys() const;

    // Removes all chunks.
    void clear();

    // Returns the total number of loaded chunks.
    size_t chunk_count() const { return chunks_.size(); }

    // --- Gameplay config ---

    // Runtime gameplay configuration (collapse, gravity fall, etc.).
    // Mutable at runtime, separate from frozen WorldGenConfigSnapshot.
    GameplayConfig& gameplay_config() { return gameplay_config_; }
    const GameplayConfig& gameplay_config() const { return gameplay_config_; }

    void set_gameplay_config(const GameplayConfig& config) {
        gameplay_config_ = config;
    }

    // --- Simulation tick ---

    // Current simulation tick counter, set by TickSystem each frame.
    // Subsystems read this instead of maintaining their own counters.
    int64_t current_tick() const { return current_tick_; }
    void set_current_tick(int64_t t) { current_tick_ = t; }

    // --- World gen config (read-only reference) ---

    // Frozen world generation config snapshot. Provides access to
    // PlanetConfig, material definitions, etc. for physics systems.
    std::shared_ptr<const WorldGenConfigSnapshot> worldgen_config() const {
        return worldgen_config_;
    }

    void set_worldgen_config(
        std::shared_ptr<const WorldGenConfigSnapshot> config) {
        worldgen_config_ = config ? std::move(config) : make_empty_world_gen_config();
    }

    // --- Block entity registry ---

    BlockEntityRegistry& block_entity_registry() { return block_entity_registry_; }
    const BlockEntityRegistry& block_entity_registry() const { return block_entity_registry_; }

    // --- Dynamic mobile structures ---

    mobile_structure::DynamicStructureRegistry& mobile_structure_registry() {
        return mobile_structure_registry_;
    }

    const mobile_structure::DynamicStructureRegistry& mobile_structure_registry() const {
        return mobile_structure_registry_;
    }

    // --- Block physics events ---

    // Enqueue a block physics event (e.g., after mining a block).
    void push_physics_event(const BlockPhysicsEvent& event) {
        physics_events_.push(event);
    }

    // Dequeue the next physics event. Returns false if empty.
    bool pop_physics_event(BlockPhysicsEvent& out) {
        if (physics_events_.empty()) return false;
        out = physics_events_.front();
        physics_events_.pop();
        return true;
    }

    // Returns the number of pending physics events.
    size_t physics_event_count() const { return physics_events_.size(); }

    // --- Machine collision overlay ---

    // Sparse overlay of cells occupied by machines (furnaces, campfires, ...).
    // Consumed by chunk collision generation so machines get collision
    // coverage without per-object Godot StaticBody3D nodes. See design doc
    // docs/专用引擎性能优化方向.md (physics layer).
    MachineCollisionOverlay& machine_collision_overlay() {
        return machine_collision_overlay_;
    }
    const MachineCollisionOverlay& machine_collision_overlay() const {
        return machine_collision_overlay_;
    }

    // Convenience: mark/clear a machine-occupied cell.
    void set_machine_collision(const std::string& dimension_id,
                              int cell_x, int cell_y, int cell_z,
                              bool occupied) {
        machine_collision_overlay_.set(
            dimension_id, cell_x, cell_y, cell_z, occupied);
    }

    // Convenience: query a machine-occupied cell.
    bool is_machine_collision(const std::string& dimension_id,
                              int cell_x, int cell_y, int cell_z) const {
        return machine_collision_overlay_.is_occupied(
            dimension_id, cell_x, cell_y, cell_z);
    }

private:
    ChunkKey make_key(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z) const;

    std::unordered_map<ChunkKey, ChunkData> chunks_;

    // Runtime gameplay configuration.
    GameplayConfig gameplay_config_;

    // Current simulation tick (set by TickSystem).
    int64_t current_tick_ = 0;

    // Frozen world generation config snapshot.
    std::shared_ptr<const WorldGenConfigSnapshot> worldgen_config_;

    // Block physics event queue.
    std::queue<BlockPhysicsEvent> physics_events_;

    // Block entity registry (trees, machines, etc.).
    BlockEntityRegistry block_entity_registry_;

    // Dynamic mobile structures (ships, contraptions, future stations).
    mobile_structure::DynamicStructureRegistry mobile_structure_registry_;

    // Machine collision overlay: sparse set of cells occupied by machines so
    // the chunk collision generator can include them without per-object
    // Godot physics nodes.
    MachineCollisionOverlay machine_collision_overlay_;
};

// --- Inline implementations ---

inline ChunkKey WorldData::make_key(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    return ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z};
}

inline bool WorldData::has_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    return chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z)) != chunks_.end();
}

inline ChunkData* WorldData::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline const ChunkData* WorldData::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline void WorldData::set_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    ChunkData chunk) {
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunks_[make_key(dimension_id, chunk_x, chunk_y, chunk_z)] = std::move(chunk);
}

inline void WorldData::remove_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    chunks_.erase(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
}

inline std::vector<ChunkKey> WorldData::all_chunk_keys() const {
    std::vector<ChunkKey> keys;
    keys.reserve(chunks_.size());
    for (const auto& pair : chunks_) {
        keys.push_back(pair.first);
    }
    return keys;
}

inline void WorldData::clear() {
    chunks_.clear();
    mobile_structure_registry_.clear();
}

} // namespace science_and_theology
