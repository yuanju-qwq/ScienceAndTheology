#include "gd_chunk_view.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

GDChunkView::GDChunkView() {
}

GDChunkView::~GDChunkView() {
}

void GDChunkView::set_layer_id(const String& id) {
    layer_id_ = id;
}

String GDChunkView::get_layer_id() const {
    return layer_id_;
}

void GDChunkView::set_chunk_x(int x) {
    chunk_x_ = x;
}

int GDChunkView::get_chunk_x() const {
    return chunk_x_;
}

void GDChunkView::set_chunk_y(int y) {
    chunk_y_ = y;
}

int GDChunkView::get_chunk_y() const {
    return chunk_y_;
}

void GDChunkView::set_worldgen_config(Resource* config) {
    auto* worldgen_config = Object::cast_to<GDWorldGenConfig>(config);
    if (config != nullptr && worldgen_config == nullptr) {
        UtilityFunctions::push_warning(
            "GDChunkView: worldgen_config must be a GDWorldGenConfig resource.");
        return;
    }

    if (worldgen_config != nullptr) {
        worldgen_config_resource_ = Ref<GDWorldGenConfig>(worldgen_config);
    } else {
        worldgen_config_resource_.unref();
    }
    palette_.set_config(worldgen_config != nullptr
        ? worldgen_config->get_snapshot()
        : nullptr);
}

Resource* GDChunkView::get_worldgen_config() const {
    return worldgen_config_resource_.ptr();
}

void GDChunkView::update_from_terrain(const Dictionary& terrain_data) {
    clear();

    int size_x = static_cast<int>(terrain_data.get("size_x", 0));
    int size_y = static_cast<int>(terrain_data.get("size_y", 0));

    if (size_x <= 0 || size_y <= 0) {
        UtilityFunctions::push_warning(
            "GDChunkView: invalid terrain dimensions ", size_x, "x", size_y);
        return;
    }

    PackedByteArray materials = terrain_data.get("materials", PackedByteArray());
    if (materials.size() < size_x * size_y) {
        UtilityFunctions::push_warning(
            "GDChunkView: materials array too small");
        return;
    }

    std::string layer_str = layer_id_.utf8().get_data();
    int cell_count = size_x * size_y;

    for (int i = 0; i < cell_count; ++i) {
        int ly = i / size_x;
        int lx = i % size_x;
        uint8_t mat = materials[i];

        if (mat == 0) {
            continue;
        }

        int variant_seed = chunk_x_ * 31 + chunk_y_ * 17 + lx * 7 + ly * 3;
        apply_tile(lx, ly, mat, variant_seed);
    }
}

void GDChunkView::apply_tile(int local_x, int local_y, uint8_t material, int variant_seed) {
    std::string layer_str = layer_id_.utf8().get_data();
    TileRenderInfo info = palette_.get_tile(
        static_cast<int>(material), layer_str, variant_seed);

    if (!info.enabled) {
        return;
    }

    set_cell(
        Vector2i(local_x, local_y),
        info.source_id,
        Vector2i(info.atlas_coords.x, info.atlas_coords.y));
}

void GDChunkView::clear_chunk() {
    clear();
}

Dictionary GDChunkView::get_chunk_key() const {
    Dictionary key;
    key["layer"] = layer_id_;
    key["chunk_x"] = chunk_x_;
    key["chunk_y"] = chunk_y_;
    return key;
}

void GDChunkView::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_layer_id", "id"),
                         &GDChunkView::set_layer_id);
    ClassDB::bind_method(D_METHOD("get_layer_id"),
                         &GDChunkView::get_layer_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layer_id"),
                 "set_layer_id", "get_layer_id");

    ClassDB::bind_method(D_METHOD("set_chunk_x", "x"),
                         &GDChunkView::set_chunk_x);
    ClassDB::bind_method(D_METHOD("get_chunk_x"),
                         &GDChunkView::get_chunk_x);
    ClassDB::bind_method(D_METHOD("set_chunk_y", "y"),
                         &GDChunkView::set_chunk_y);
    ClassDB::bind_method(D_METHOD("get_chunk_y"),
                         &GDChunkView::get_chunk_y);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_x"),
                 "set_chunk_x", "get_chunk_x");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_y"),
                 "set_chunk_y", "get_chunk_y");

    ClassDB::bind_method(D_METHOD("set_worldgen_config", "config"),
                         &GDChunkView::set_worldgen_config);
    ClassDB::bind_method(D_METHOD("get_worldgen_config"),
                         &GDChunkView::get_worldgen_config);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "worldgen_config",
                 PROPERTY_HINT_RESOURCE_TYPE, "GDWorldGenConfig"),
                 "set_worldgen_config", "get_worldgen_config");

    ClassDB::bind_method(D_METHOD("update_from_terrain", "terrain_data"),
                         &GDChunkView::update_from_terrain);
    ClassDB::bind_method(D_METHOD("clear_chunk"),
                         &GDChunkView::clear_chunk);
    ClassDB::bind_method(D_METHOD("get_chunk_key"),
                         &GDChunkView::get_chunk_key);

    ADD_SIGNAL(MethodInfo("chunk_view_updated",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));
}

} // namespace science_and_theology
