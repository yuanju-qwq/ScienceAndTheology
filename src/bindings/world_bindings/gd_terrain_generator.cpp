#include "gd_terrain_generator.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

GDTerrainGenerator::GDTerrainGenerator()
    : seed_(0) {
    rebuild_generator();
}

GDTerrainGenerator::~GDTerrainGenerator() = default;

int64_t GDTerrainGenerator::get_seed() const {
    return seed_;
}

void GDTerrainGenerator::set_seed(int64_t seed) {
    if (seed_ == seed) {
        return;
    }
    seed_ = seed;
    rebuild_generator();
}

void GDTerrainGenerator::rebuild_generator() {
    generator_ = std::make_unique<TerrainGenerator>(
        WorldSeed(static_cast<uint64_t>(seed_)));
}

godot::Dictionary GDTerrainGenerator::generate_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    Dictionary result;

    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDTerrainGenerator: generator not initialized");
        return result;
    }

    std::string layer_str = layer_id.utf8().get_data();

    ChunkData chunk = generator_->generate_chunk(layer_str, chunk_x, chunk_y);
    const TerrainData& terrain = chunk.terrain;

    int cell_count = terrain.size_x * terrain.size_y;
    PackedByteArray materials;
    PackedInt32Array flags;
    materials.resize(cell_count);
    flags.resize(cell_count);

    for (int i = 0; i < cell_count; ++i) {
        materials[i] = static_cast<uint8_t>(terrain.cells[i].material);
        flags[i] = static_cast<int32_t>(terrain.cells[i].flags);
    }

    result["size_x"] = terrain.size_x;
    result["size_y"] = terrain.size_y;
    result["materials"] = materials;
    result["flags"] = flags;

    return result;
}

void GDTerrainGenerator::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_seed"),
                         &GDTerrainGenerator::get_seed);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                         &GDTerrainGenerator::set_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"),
                 "set_seed", "get_seed");

    ClassDB::bind_method(D_METHOD("generate_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDTerrainGenerator::generate_chunk);
}

} // namespace science_and_theology