#include "gd_world_data.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/world/chunk_data.hpp"

VARIANT_ENUM_CAST(science_and_theology::GDWorldData::ChunkStateConst)
VARIANT_ENUM_CAST(science_and_theology::GDWorldData::MaterialConst)

namespace science_and_theology {

using namespace godot;

GDWorldData::GDWorldData()
    : seed_(0) {
    rebuild_generator();
}

GDWorldData::~GDWorldData() {
    delete generator_;
    generator_ = nullptr;
}

int64_t GDWorldData::get_seed() const {
    return seed_;
}

void GDWorldData::set_seed(int64_t seed) {
    if (seed_ == seed) {
        return;
    }
    seed_ = seed;
    rebuild_generator();
}

void GDWorldData::rebuild_generator() {
    delete generator_;
    generator_ = nullptr;
    generator_ = new TerrainGenerator(
        WorldSeed(static_cast<uint64_t>(seed_)));
}

godot::Dictionary GDWorldData::get_or_generate_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    static const Dictionary kEmptyResult;

    if (generator_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDWorldData: generator not initialized");
        return kEmptyResult;
    }

    std::string layer_str = layer_id.utf8().get_data();

    // Return existing chunk if already generated.
    if (world_.has_chunk(layer_str, chunk_x, chunk_y)) {
        const ChunkData* existing = world_.get_chunk(
            layer_str, chunk_x, chunk_y);
        if (existing != nullptr) {
            return terrain_to_dict(existing->terrain);
        }
    }

    // Generate new chunk.
    ChunkData chunk = generator_->generate_chunk(layer_str, chunk_x, chunk_y);

    // Snapshot terrain before moving chunk into world.
    Dictionary result = terrain_to_dict(chunk.terrain);

    world_.set_chunk(layer_str, chunk_x, chunk_y, std::move(chunk));

    // Notify Godot that a new chunk is ready for rendering.
    emit_signal("chunk_ready", layer_id, chunk_x, chunk_y);

    return result;
}

int64_t GDWorldData::get_chunk_state(
    const godot::String& layer_id, int chunk_x, int chunk_y) const {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return -1;
    }
    return static_cast<int64_t>(chunk->state);
}

void GDWorldData::set_chunk_state(
    const godot::String& layer_id, int chunk_x, int chunk_y, int state) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        UtilityFunctions::push_warning(
            "GDWorldData: cannot set state, chunk not found: ",
            layer_id, " ", chunk_x, " ", chunk_y);
        return;
    }
    chunk->state = static_cast<ChunkState>(state);
}

bool GDWorldData::has_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    return world_.has_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y);
}

void GDWorldData::set_chunk_from_dict(
    const godot::String& layer_id, int chunk_x, int chunk_y,
    const godot::Dictionary& data) {
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.state = ChunkState::GENERATED;

    int size_x = static_cast<int>(data.get("size_x", 0));
    int size_y = static_cast<int>(data.get("size_y", 0));
    if (size_x <= 0 || size_y <= 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: invalid chunk size in data dictionary");
        return;
    }

    chunk.terrain.resize(size_x, size_y);

    PackedByteArray materials = data.get("materials", PackedByteArray());
    PackedInt32Array flags = data.get("flags", PackedInt32Array());

    int cell_count = size_x * size_y;
    if (materials.size() < cell_count || flags.size() < cell_count) {
        UtilityFunctions::push_warning(
            "GDWorldData: materials/flags arrays too small for chunk dimensions");
        return;
    }

    for (int i = 0; i < cell_count; ++i) {
        chunk.terrain.cells[i].material =
            static_cast<TerrainMaterial>(materials[i]);
        chunk.terrain.cells[i].flags =
            static_cast<uint32_t>(flags[i]);
    }

    world_.set_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y,
                     std::move(chunk));
}

godot::Dictionary GDWorldData::get_chunk_terrain(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Dictionary();
    }
    return terrain_to_dict(chunk->terrain);
}

void GDWorldData::remove_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    world_.remove_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y);
}

godot::Array GDWorldData::get_all_chunk_keys() const {
    Array result;
    for (const auto& key : world_.all_chunk_keys()) {
        Dictionary key_dict;
        key_dict["layer"] = String(key.layer_id.c_str());
        key_dict["chunk_x"] = key.chunk_x;
        key_dict["chunk_y"] = key.chunk_y;
        result.append(key_dict);
    }
    return result;
}

void GDWorldData::clear() {
    world_.clear();
}

int64_t GDWorldData::get_chunk_count() const {
    return static_cast<int64_t>(world_.chunk_count());
}

godot::Dictionary GDWorldData::terrain_to_dict(
    const TerrainData& terrain) const {
    Dictionary result;
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

void GDWorldData::_bind_methods() {
    // Seed property.
    ClassDB::bind_method(D_METHOD("get_seed"),
                         &GDWorldData::get_seed);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                         &GDWorldData::set_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"),
                 "set_seed", "get_seed");

    // Chunk generation and query.
    ClassDB::bind_method(D_METHOD("get_or_generate_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_or_generate_chunk);
    ClassDB::bind_method(D_METHOD("get_chunk_state", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_state);
    ClassDB::bind_method(D_METHOD("set_chunk_state", "layer_id", "chunk_x", "chunk_y", "state"),
                         &GDWorldData::set_chunk_state);
    ClassDB::bind_method(D_METHOD("has_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::has_chunk);
    ClassDB::bind_method(D_METHOD("set_chunk_from_dict", "layer_id", "chunk_x", "chunk_y", "data"),
                         &GDWorldData::set_chunk_from_dict);
    ClassDB::bind_method(D_METHOD("get_chunk_terrain", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_terrain);
    ClassDB::bind_method(D_METHOD("remove_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::remove_chunk);
    ClassDB::bind_method(D_METHOD("get_all_chunk_keys"),
                         &GDWorldData::get_all_chunk_keys);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDWorldData::clear);
    ClassDB::bind_method(D_METHOD("get_chunk_count"),
                         &GDWorldData::get_chunk_count);

    // Signal: emitted when a new chunk has been generated and stored.
    ADD_SIGNAL(MethodInfo("chunk_ready",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));

    // Chunk state constants.
    BIND_ENUM_CONSTANT(STATE_UNLOADED);
    BIND_ENUM_CONSTANT(STATE_GENERATING);
    BIND_ENUM_CONSTANT(STATE_GENERATED);
    BIND_ENUM_CONSTANT(STATE_ACTIVE);
    BIND_ENUM_CONSTANT(STATE_SLEEPING);

    // Terrain material constants.
    BIND_ENUM_CONSTANT(MAT_AIR);
    BIND_ENUM_CONSTANT(MAT_STONE);
    BIND_ENUM_CONSTANT(MAT_DIRT);
    BIND_ENUM_CONSTANT(MAT_SAND);
    BIND_ENUM_CONSTANT(MAT_WATER);
    BIND_ENUM_CONSTANT(MAT_LAVA);
    BIND_ENUM_CONSTANT(MAT_ORE_IRON);
    BIND_ENUM_CONSTANT(MAT_ORE_COPPER);
    BIND_ENUM_CONSTANT(MAT_ORE_COAL);
}

} // namespace science_and_theology