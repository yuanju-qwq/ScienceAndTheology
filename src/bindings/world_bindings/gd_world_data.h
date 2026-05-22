#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/world/world_data.hpp"
#include "core/world_gen/terrain_generator.hpp"
#include "core/world_gen/world_seed.hpp"

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

    // Terrain material enum matching C++ TerrainMaterial.
    enum MaterialConst {
        MAT_AIR       = 0,
        MAT_STONE     = 1,
        MAT_DIRT      = 2,
        MAT_SAND      = 3,
        MAT_WATER     = 4,
        MAT_LAVA      = 5,
        MAT_ORE_IRON  = 6,
        MAT_ORE_COPPER = 7,
        MAT_ORE_COAL  = 8,
    };

    // Seed property. Changing the seed rebuilds the internal generator.
    int64_t get_seed() const;
    void set_seed(int64_t seed);

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
    godot::Dictionary get_chunk_terrain(const godot::String& layer_id,
                                        int chunk_x, int chunk_y);

    // Removes a chunk entirely.
    void remove_chunk(const godot::String& layer_id,
                      int chunk_x, int chunk_y);

    // Returns all loaded chunk keys as an Array of Dictionaries.
    godot::Array get_all_chunk_keys() const;

    // Clears all loaded chunks.
    void clear();

    // Returns the total number of loaded chunks.
    int64_t get_chunk_count() const;

protected:
    static void _bind_methods();

private:
    void rebuild_generator();
    godot::Dictionary terrain_to_dict(const TerrainData& terrain) const;

    WorldData world_;
    TerrainGenerator* generator_ = nullptr;
    int64_t seed_ = 0;
};

} // namespace science_and_theology