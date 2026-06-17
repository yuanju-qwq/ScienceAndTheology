#include "gd_terrain_generator.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

GDTerrainGenerator::GDTerrainGenerator()
    : seed_(0) {
    worldgen_config_ = make_empty_world_gen_config();
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

void GDTerrainGenerator::set_worldgen_config(Resource* config) {
    auto* worldgen_config = Object::cast_to<GDWorldGenConfig>(config);
    if (config != nullptr && worldgen_config == nullptr) {
        UtilityFunctions::push_warning(
            "GDTerrainGenerator: worldgen_config must be a GDWorldGenConfig resource.");
        return;
    }

    if (worldgen_config != nullptr) {
        worldgen_config_resource_ = Ref<GDWorldGenConfig>(worldgen_config);
    } else {
        worldgen_config_resource_.unref();
    }
    worldgen_config_ = worldgen_config != nullptr
        ? worldgen_config->get_snapshot()
        : make_empty_world_gen_config();
    rebuild_generator();
}

Resource* GDTerrainGenerator::get_worldgen_config() const {
    return worldgen_config_resource_.ptr();
}

int64_t GDTerrainGenerator::get_worldgen_content_hash() const {
    if (!worldgen_config_) {
        return 0;
    }
    return static_cast<int64_t>(worldgen_config_->content_hash);
}

void GDTerrainGenerator::rebuild_generator() {
    if (!worldgen_config_) {
        worldgen_config_ = make_empty_world_gen_config();
    }
    generator_ = std::make_unique<TerrainGenerator>(
        WorldSeed(static_cast<uint64_t>(seed_)), worldgen_config_);
}

godot::Dictionary GDTerrainGenerator::generate_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    Dictionary result;

    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDTerrainGenerator: generator not initialized");
        return result;
    }

    std::string dimension_str = dimension_id.utf8().get_data();

    ChunkData chunk = generator_->generate_chunk(
        dimension_str, chunk_x, chunk_y, chunk_z);
    const TerrainData& terrain = chunk.terrain;

    int cell_count = terrain.size_x * terrain.size_y * terrain.size_z;
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
    result["size_z"] = terrain.size_z;
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

    ClassDB::bind_method(D_METHOD("set_worldgen_config", "config"),
                         &GDTerrainGenerator::set_worldgen_config);
    ClassDB::bind_method(D_METHOD("get_worldgen_config"),
                         &GDTerrainGenerator::get_worldgen_config);
    ClassDB::bind_method(D_METHOD("get_worldgen_content_hash"),
                         &GDTerrainGenerator::get_worldgen_content_hash);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "worldgen_config",
                 PROPERTY_HINT_RESOURCE_TYPE, "GDWorldGenConfig"),
                 "set_worldgen_config", "get_worldgen_config");

    ClassDB::bind_method(D_METHOD("generate_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDTerrainGenerator::generate_chunk);
}

} // namespace science_and_theology
