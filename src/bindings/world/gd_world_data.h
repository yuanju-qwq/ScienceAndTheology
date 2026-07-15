#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <vector>

#include "core/world/world_data.hpp"
#include "core/world_gen/terrain_generator.hpp"
#include "core/world_gen/world_seed.hpp"
#include "core/save/save_manager.hpp"
#include "gd_world_gen_config.h"

namespace science_and_theology {

// GDExtension wrapper for world data with integrated chunk generation.
// Godot uses this as the single entry point for all world operations.
//
// Usage in GDScript:
//   var world = GDWorldData.new()
//   world.seed = 12345
//   var data = world.get_or_generate_chunk("overworld", 0, 0, 0)
//   if world.has_chunk("overworld", 0, 0, 0):
//       var terrain = world.get_chunk_terrain("overworld", 0, 0, 0)
class GDWorldData : public godot::Resource {
    GDCLASS(GDWorldData, godot::Resource)

public:
    GDWorldData();
    ~GDWorldData() override;

    // Chunk state enum matching C++ ChunkState.
    enum ChunkStateConst {
        STATE_UNLOADED   = 0,
        STATE_GENERATING = 1,
        STATE_GENERATED  = 2,
        STATE_ACTIVE     = 3,
        STATE_SLEEPING   = 4,
    };

    // Seed property. Changing the seed rebuilds the internal generator.
    int64_t get_seed() const;
    void set_seed(int64_t seed);

    // Frozen terrain/world-generation content configuration.
    void set_worldgen_config(godot::Resource* config);
    godot::Resource* get_worldgen_config() const;
    int64_t get_worldgen_content_hash() const;
    std::shared_ptr<const WorldGenConfigSnapshot> get_worldgen_snapshot() const;
    godot::Dictionary get_terrain_material_def(int64_t material_id) const;

    // Generates a chunk if it doesn't exist, then returns its terrain data.
    // Emits chunk_ready signal when a new chunk is generated.
    // Returns: { "size_x": int, "size_y": int, "size_z": int,
    //            "materials": PackedByteArray, "flags": PackedInt32Array }
    godot::Dictionary get_or_generate_chunk(
        const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z);

    // Returns the current state of a chunk as an int (see ChunkStateConst).
    // Returns -1 if the chunk has never been touched.
    int64_t get_chunk_state(const godot::String& dimension_id,
                            int chunk_x, int chunk_y, int chunk_z) const;

    // Sets a chunk's state. Used by Godot ChunkManager to mark ACTIVE/SLEEPING.
    void set_chunk_state(const godot::String& dimension_id,
                         int chunk_x, int chunk_y, int chunk_z, int state);

    // Checks if a chunk exists in any state.
    bool has_chunk(const godot::String& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z);

    // Stores terrain data from a Dictionary.
    void set_chunk_from_dict(const godot::String& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z,
                             const godot::Dictionary& data);

    // Retrieves terrain data for a chunk as a Dictionary.
    // Includes connectors array if present.
    godot::Dictionary get_chunk_terrain(const godot::String& dimension_id,
                                        int chunk_x, int chunk_y, int chunk_z);

    // Retrieves only connector placements for a chunk.
    godot::Array get_chunk_connectors(const godot::String& dimension_id,
                                      int chunk_x, int chunk_y, int chunk_z);

    // Retrieves only mechanism placements for a chunk.
    godot::Array get_chunk_mechanisms(const godot::String& dimension_id,
                                      int chunk_x, int chunk_y, int chunk_z);

    // Returns entity IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_entities(const godot::String& dimension_id,
                                    int chunk_x, int chunk_y, int chunk_z);

    // Returns machine IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_machines(const godot::String& dimension_id,
                                    int chunk_x, int chunk_y, int chunk_z);

    // Returns connector runtime IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_connector_ids(const godot::String& dimension_id,
                                         int chunk_x, int chunk_y, int chunk_z);

    // Registers an entity ID in this chunk. Returns true on success.
    bool add_entity_to_chunk(const godot::String& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z,
                             int64_t entity_id);

    // Registers a machine ID in this chunk. Returns true on success.
    bool add_machine_to_chunk(const godot::String& dimension_id,
                              int chunk_x, int chunk_y, int chunk_z,
                              int64_t machine_id);

    // Registers a connector runtime ID in this chunk. Returns true on success.
    bool add_connector_id_to_chunk(const godot::String& dimension_id,
                                   int chunk_x, int chunk_y, int chunk_z,
                                   int64_t connector_id);

    // Terrain cell access (single cell get/set).
    godot::Dictionary get_terrain_cell(const godot::String& dimension_id,
                                       int chunk_x, int chunk_y, int chunk_z,
                                       int local_x, int local_y, int local_z);
    bool set_terrain_cell(const godot::String& dimension_id,
                          int chunk_x, int chunk_y, int chunk_z,
                          int local_x, int local_y, int local_z, int material);

    // Removes a chunk entirely.
    void remove_chunk(const godot::String& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z);

    // Returns all loaded chunk keys as an Array of Dictionaries.
    godot::Array get_all_chunk_keys() const;

    // Clears all loaded chunks.
    void clear();

    // --- Async chunk generation ---

    // Non-blocking: enqueues chunk generation on the worker pool.
    // Chunk is automatically stored when ready and chunk_ready signal is
    // emitted during the next call to process_async_results().
    void request_chunk_async(const godot::String& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z);

    // Must be called every frame (e.g., from _process).
    // Collects completed async generations, stores them in the world,
    // and emits chunk_ready signals on the main thread.
    // Returns the number of chunks that were completed this frame.
    int64_t process_async_results();

    // Returns the number of pending + in-progress async chunk requests.
    int64_t get_async_pending_count() const;

    // Returns the number of worker threads in the pool.
    int64_t get_worker_thread_count() const;

    // Returns the total number of completed async chunk generations.
    int64_t get_async_completed_count() const;

    // Returns the number of completed async chunk generations waiting to be
    // committed on the main thread.
    int64_t get_async_result_queue_size() const;

    // Returns true if a chunk is currently queued, generating, or waiting for
    // process_async_results().
    bool is_chunk_async_pending(const godot::String& dimension_id,
                                int chunk_x, int chunk_y, int chunk_z) const;

    // Maximum async queue size. When full, request_chunk_async silently
    // drops new requests to prevent unbounded memory growth.
    // Default: 256. Set to 0 for unlimited (not recommended).
    int64_t get_max_async_queue_size() const;
    void set_max_async_queue_size(int64_t size);

    // Maximum completed async chunks to commit per process_async_results() call.
    // Default: 4. Set to 0 for unlimited.
    int64_t get_max_async_results_per_frame() const;
    void set_max_async_results_per_frame(int64_t count);

    // --- Save / load ---

    // Lists save names with valid universe_header.bin in a base directory.
    static godot::Array list_saves(const godot::String& base_saves_dir);

    // --- Per-dimension save / load / unload ---

    // Saves only chunks belonging to a specific dimension to disk.
    // Returns the number of chunks saved, or -1 on error.
    int64_t save_dimension(const godot::String& save_dir,
                           const godot::String& dimension_id);

    // Loads only chunks belonging to a specific dimension from disk.
    // Does NOT clear existing chunks. Returns the number of chunks loaded,
    // or -1 on error.
    int64_t load_dimension(const godot::String& save_dir,
                           const godot::String& dimension_id);

    // Removes all chunks belonging to a specific dimension from memory.
    // Returns the number of chunks removed.
    int64_t unload_dimension(const godot::String& dimension_id);

    // Returns the number of chunks currently loaded for a specific dimension.
    int64_t get_dimension_chunk_count(const godot::String& dimension_id) const;

    // --- Universe header ---

    // Writes a universe header (seed + mode) to the save root directory.
    static bool write_universe_header(const godot::String& save_dir,
                                      int64_t seed,
                                      const godot::String& universe_mode);

    // Reads a universe header. Returns Dictionary with keys:
    //   "ok": bool, "seed": int, "universe_mode": String
    static godot::Dictionary read_universe_header(const godot::String& save_dir);

    // Lists all planet dimension IDs in a save directory.
    static godot::Array list_planets(const godot::String& save_dir);

    // --- Planet data (header + summary binary) ---

    // Writes a planet_data.bin file with header and optional summary.
    // The summary_dict follows PlanetSummary.to_dict() format.
    // Pass an empty Dictionary to write header-only (no summary).
    static bool write_planet_data(const godot::String& planet_dir,
                                  int64_t seed,
                                  const godot::String& dimension_id,
                                  const godot::Dictionary& summary_dict);

    // Reads a planet_data.bin file. Returns Dictionary with keys:
    //   "ok": bool, "seed": int, "dimension_id": String,
    //   "has_summary": bool, "summary": Dictionary (empty if no summary)
    static godot::Dictionary read_planet_data(const godot::String& planet_dir);

    // ---

    // ---

    // Returns the total number of loaded chunks.
    int64_t get_chunk_count() const;

    // --- Machine collision overlay ---

    // Mark or unmark a cell as machine-occupied in the overlay. When marked,
    // chunk collision generation will include this cell so the machine gets
    // collision coverage without a per-object Godot StaticBody3D.
    // Current runtime boundary: docs/项目架构与运行时.md.
    void set_machine_collision(const godot::String& dimension_id,
                               int32_t cell_x, int32_t cell_y, int32_t cell_z,
                               bool occupied);

    // Returns true if a cell is marked as machine-occupied.
    bool is_machine_collision(const godot::String& dimension_id,
                              int32_t cell_x, int32_t cell_y,
                              int32_t cell_z) const;

    // Returns a dense per-cell mask for a chunk: 1 if the cell is
    // machine-occupied, 0 otherwise. Mask layout matches
    // GDChunkHelper::terrain_index ordering so callers can index it
    // alongside the terrain materials array.
    godot::PackedByteArray get_chunk_machine_collision_mask(
        const godot::String& dimension_id,
        int32_t chunk_x, int32_t chunk_y, int32_t chunk_z,
        int32_t size_x, int32_t size_y, int32_t size_z) const;

    // Returns the total number of machine-occupied cells across all dimensions.
    int64_t get_machine_collision_count() const;

    // Removes all machine collision entries for a dimension.
    // Returns the number of entries removed.
    int64_t clear_machine_collision_dimension(const godot::String& dimension_id);

    // --- Gameplay config ---

    // Returns the gameplay config as a Dictionary.
    godot::Dictionary get_gameplay_config() const;

    // Sets the gameplay config from a Dictionary.
    void set_gameplay_config(const godot::Dictionary& config);

    // Returns a specific gameplay config value for a dimension.
    // Falls back to global defaults if no planet override exists.
    godot::Dictionary get_gameplay_config_for_dimension(
        const godot::String& dimension_id) const;

    // Returns a pointer to the underlying C++ WorldData.
    // Used by TickSystem and other simulation systems.
    WorldData* get_world_ptr() { return &world_; }
    const WorldData* get_world_ptr() const { return &world_; }

protected:
    static void _bind_methods();

private:
    void rebuild_generator();
    godot::Dictionary terrain_drop_to_dict(const TerrainDropDef& drop) const;
    godot::Dictionary terrain_material_to_dict(const TerrainMaterialDef& def) const;
    godot::Dictionary terrain_to_dict(const TerrainData& terrain) const;
    godot::Array connectors_to_array(
        const std::vector<ConnectorPlacement>& connectors) const;
    godot::Array mechanisms_to_array(
        const std::vector<MechanismPlacement>& mechanisms) const;
    godot::Array entity_ids_to_array(
        const std::vector<EntityId>& ids) const;
    static std::vector<EntityId> array_to_entity_ids(const godot::Array& arr);

    // Async generation internals.
    struct AsyncChunkResult {
        std::string dimension_id;
        int chunk_x = 0;
        int chunk_y = 0;
        int chunk_z = 0;
        ChunkData chunk;
    };

    struct AsyncChunkTaskData {
        GDWorldData* world_data;
        TerrainGenerator* gen;
        std::string dimension_id;
        int chunk_x;
        int chunk_y;
        int chunk_z;
    };

    struct AsyncChunkKey {
        std::string dimension_id;
        int chunk_x = 0;
        int chunk_y = 0;
        int chunk_z = 0;

        bool operator==(const AsyncChunkKey& other) const {
            return dimension_id == other.dimension_id &&
                   chunk_x == other.chunk_x &&
                   chunk_y == other.chunk_y &&
                   chunk_z == other.chunk_z;
        }
    };

    struct AsyncChunkKeyHash {
        size_t operator()(const AsyncChunkKey& key) const {
            size_t h = std::hash<std::string>()(key.dimension_id);
            h ^= std::hash<int>()(key.chunk_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(key.chunk_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(key.chunk_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static void _async_chunk_callback(void* userdata);
    void _drain_all_async_tasks();

    WorldData world_;
    std::unique_ptr<TerrainGenerator> generator_;
    godot::Ref<GDWorldGenConfig> worldgen_config_resource_;
    std::shared_ptr<const WorldGenConfigSnapshot> worldgen_config_;
    int64_t seed_ = 0;

    // Async generation state.
    static constexpr int64_t kDefaultMaxAsyncQueueSize = 256;
    static constexpr int64_t kDefaultMaxAsyncResultsPerFrame = 4;
    std::vector<godot::WorkerThreadPool::TaskID> active_native_tasks_;
    std::queue<AsyncChunkResult> async_results_;
    std::unordered_set<AsyncChunkKey, AsyncChunkKeyHash> pending_async_chunks_;
    mutable std::mutex async_results_mutex_;
    int64_t async_completed_count_ = 0;
    int64_t max_async_queue_size_ = kDefaultMaxAsyncQueueSize;
    int64_t max_async_results_per_frame_ = kDefaultMaxAsyncResultsPerFrame;
};

} // namespace science_and_theology
