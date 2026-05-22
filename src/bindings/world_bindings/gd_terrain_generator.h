#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/world/world_data.hpp"
#include "core/world_gen/terrain_generator.hpp"

namespace science_and_theology {

// GDExtension wrapper for the terrain generator.
// Exposes chunk generation to GDScript for rendering.
//
// Usage in GDScript:
//   var gen = GDTerrainGenerator.new()
//   gen.seed = 12345
//   var data = gen.generate_chunk("surface", 0, 0)
//   # data is a Dictionary with "size_x", "size_y", "materials", "flags"
class GDTerrainGenerator : public godot::Resource {
    GDCLASS(GDTerrainGenerator, godot::Resource)

public:
    GDTerrainGenerator();
    ~GDTerrainGenerator() override;

    // Seed property.
    int64_t get_seed() const;
    void set_seed(int64_t seed);

    // Generates a chunk and returns its terrain data as a Dictionary.
    // Returns: { "size_x": int, "size_y": int,
    //            "materials": PackedByteArray, "flags": PackedInt32Array }
    godot::Dictionary generate_chunk(
        const godot::String& layer_id, int chunk_x, int chunk_y);

protected:
    static void _bind_methods();

private:
    void rebuild_generator();

    TerrainGenerator* generator_ = nullptr;
    int64_t seed_ = 0;
};

} // namespace science_and_theology