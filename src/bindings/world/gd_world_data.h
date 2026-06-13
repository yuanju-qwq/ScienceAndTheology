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
//   var data = world.get_or_generate_chunk("surface", 0, 0)
//   if world.has_chunk("surface", 0, 0):
//       var terrain = world.get_chunk_terrain("surface", 0, 0)
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
    // Returns: { "size_x": int, "size_y": int,
    //            "materials": PackedByteArray, "flags": PackedInt32Array }
    godot::Dictionary get_or_generate_chunk(
        const godot::String& layer_id, int chunk_x, int chunk_y);

    // Returns the current state of a chunk as an int (see ChunkStateConst).
    // Returns -1 if the chunk has never been touched.
    int64_t get_chunk_state(const godot::String& layer_id,
                            int chunk_x, int chunk_y) const;

    // Sets a chunk's state. Used by Godot ChunkManager to mark ACTIVE/SLEEPING.
    void set_chunk_state(const godot::String& layer_id,
                         int chunk_x, int chunk_y, int state);

    // Checks if a chunk exists in any state.
    bool has_chunk(const godot::String& layer_id, int chunk_x, int chunk_y);

    // Stores terrain data from a Dictionary.
    void set_chunk_from_dict(const godot::String& layer_id,
                             int chunk_x, int chunk_y,
                             const godot::Dictionary& data);

    // Retrieves terrain data for a chunk as a Dictionary.
    // Includes connectors array if present.
    godot::Dictionary get_chunk_terrain(const godot::String& layer_id,
                                        int chunk_x, int chunk_y);

    // Retrieves only connector placements for a chunk.
    godot::Array get_chunk_connectors(const godot::String& layer_id,
                                      int chunk_x, int chunk_y);

    // Retrieves only mechanism placements for a chunk.
    godot::Array get_chunk_mechanisms(const godot::String& layer_id,
                                      int chunk_x, int chunk_y);

    // Returns entity IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_entities(const godot::String& layer_id,
                                    int chunk_x, int chunk_y);

    // Returns machine IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_machines(const godot::String& layer_id,
                                    int chunk_x, int chunk_y);

    // Returns connector runtime IDs owned by this chunk as an Array of uint64.
    godot::Array get_chunk_connector_ids(const godot::String& layer_id,
                                         int chunk_x, int chunk_y);

    // Registers an entity ID in this chunk. Returns true on success.
    bool add_entity_to_chunk(const godot::String& layer_id,
                             int chunk_x, int chunk_y, int64_t entity_id);

    // Registers a machine ID in this chunk. Returns true on success.
    bool add_machine_to_chunk(const godot::String& layer_id,
                              int chunk_x, int chunk_y, int64_t machine_id);

    // Registers a connector runtime ID in this chunk. Returns true on success.
    bool add_connector_id_to_chunk(const godot::String& layer_id,
                                   int chunk_x, int chunk_y, int64_t connector_id);

    // Terrain cell access (single cell get/set).
    godot::Dictionary get_terrain_cell(const godot::String& layer_id,
                                       int chunk_x, int chunk_y,
                                       int local_x, int local_y);
    bool set_terrain_cell(const godot::String& layer_id,
                          int chunk_x, int chunk_y,
                          int local_x, int local_y, int material);

    // Removes a chunk entirely.
    void remove_chunk(const godot::String& layer_id,
                      int chunk_x, int chunk_y);

    // Returns all loaded chunk keys as an Array of Dictionaries.
    godot::Array get_all_chunk_keys() const;

    // Clears all loaded chunks.
    void clear();

    // --- Async chunk generation ---

    // Non-blocking: enqueues chunk generation on the worker pool.
    // Chunk is automatically stored when ready and chunk_ready signal is
    // emitted during the next call to process_async_results().
    void request_chunk_async(const godot::String& layer_id,
                             int chunk_x, int chunk_y);

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

    // Maximum async queue size. When full, request_chunk_async silently
    // drops new requests to prevent unbounded memory growth.
    // Default: 256. Set to 0 for unlimited (not recommended).
    int64_t get_max_async_queue_size() const;
    void set_max_async_queue_size(int64_t size);

    // --- Save / load ---

    // Saves all loaded chunks to a directory. Uses chunks stored in WorldData.
    // Returns the number of chunks saved, or -1 on error.
    int64_t save_world(const godot::String& save_dir);

    // Loads all chunks from a save directory into WorldData.
    // Existing chunks are cleared. Returns the world seed, or -1 on error.
    int64_t load_world(const godot::String& save_dir);

    // Lists save names with valid world_header.bin in a base directory.
    static godot::Array list_saves(const godot::String& base_saves_dir);

    // ---

    // ---

    // Returns the total number of loaded chunks.
    int64_t get_chunk_count() const;

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
        std::string layer_id;
        int chunk_x = 0;
        int chunk_y = 0;
        ChunkData chunk;
    };

    struct AsyncChunkTaskData {
        GDWorldData* world_data;
        TerrainGenerator* gen;
        std::string layer_id;
        int chunk_x;
        int chunk_y;
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
    std::vector<godot::WorkerThreadPool::TaskID> active_native_tasks_;
    std::queue<AsyncChunkResult> async_results_;
    mutable std::mutex async_results_mutex_;
    int64_t async_completed_count_ = 0;
    int64_t max_async_queue_size_ = kDefaultMaxAsyncQueueSize;
};

} // namespace science_and_theology
