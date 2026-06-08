#include "gd_chunk_manager.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/tile_map_layer.hpp>

#include "gd_world_data.h"

namespace science_and_theology {

using namespace godot;

GDChunkManager::GDChunkManager() {
}

GDChunkManager::~GDChunkManager() {
    unload_all_chunks();
}

void GDChunkManager::set_world_data(Resource* world) {
    world_data_ = world;
}

Resource* GDChunkManager::get_world_data() const {
    return world_data_;
}

void GDChunkManager::set_loaded_radius(int64_t radius) {
    loaded_radius_ = radius > 0 ? radius : 1;
}

int64_t GDChunkManager::get_loaded_radius() const {
    return loaded_radius_;
}

void GDChunkManager::set_view_radius(int64_t radius) {
    view_radius_ = radius > 0 ? radius : 1;
}

int64_t GDChunkManager::get_view_radius() const {
    return view_radius_;
}

void GDChunkManager::set_tile_size(int64_t size) {
    tile_size_ = size > 0 ? size : 32;
}

int64_t GDChunkManager::get_tile_size() const {
    return tile_size_;
}

void GDChunkManager::set_surface_container_path(const NodePath& path) {
    surface_container_path_ = path;
}

NodePath GDChunkManager::get_surface_container_path() const {
    return surface_container_path_;
}

void GDChunkManager::set_underground_container_path(const NodePath& path) {
    underground_container_path_ = path;
}

NodePath GDChunkManager::get_underground_container_path() const {
    return underground_container_path_;
}

void GDChunkManager::set_unload_distant_chunks(bool enable) {
    unload_distant_chunks_ = enable;
}

bool GDChunkManager::get_unload_distant_chunks() const {
    return unload_distant_chunks_;
}

void GDChunkManager::set_player_chunk(const String& layer, int cx, int cy) {
    player_layer_ = layer.utf8().get_data();
    player_cx_ = cx;
    player_cy_ = cy;
}

Dictionary GDChunkManager::get_player_chunk() const {
    Dictionary d;
    d["layer"] = String(player_layer_.c_str());
    d["chunk_x"] = player_cx_;
    d["chunk_y"] = player_cy_;
    return d;
}

// -- Force-loaded chunks --

void GDChunkManager::force_load_chunk(const String& layer, int cx, int cy) {
    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};
    force_loaded_chunks_.insert(pos);

    // Ensure it's loaded in WorldData and has a view if within view radius.
    ensure_chunk_loaded(layer, cx, cy);
    if (is_within_view_radius(layer_str, cx, cy)) {
        show_chunk(layer, cx, cy);
    }
}

void GDChunkManager::force_unload_chunk(const String& layer, int cx, int cy) {
    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};
    force_loaded_chunks_.erase(pos);

    // If no longer needed by any tier, unload it.
    if (!should_be_loaded(pos)) {
        unload_chunk(layer, cx, cy);
    }
}

bool GDChunkManager::is_chunk_force_loaded(const String& layer, int cx, int cy) const {
    std::string layer_str = layer.utf8().get_data();
    return force_loaded_chunks_.count(ChunkPos{layer_str, cx, cy}) > 0;
}

Array GDChunkManager::get_force_loaded_chunks() const {
    Array result;
    for (const auto& pos : force_loaded_chunks_) {
        Dictionary key;
        key["layer"] = String(pos.layer.c_str());
        key["chunk_x"] = pos.cx;
        key["chunk_y"] = pos.cy;
        result.append(key);
    }
    return result;
}

// -- Chunk management --

void GDChunkManager::on_chunk_ready(const String& layer, int cx, int cy) {
    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};

    // Track it as loaded.
    tracked_loaded_chunks_.insert(pos);

    // If it should be visible, create the view.
    if (should_be_visible(pos)) {
        show_chunk(layer, cx, cy);
    }
}

void GDChunkManager::refresh_chunks() {
    if (world_data_ == nullptr) return;

    auto* gd_world = Object::cast_to<GDWorldData>(world_data_);
    if (gd_world == nullptr) return;

    int load_radius = static_cast<int>(loaded_radius_);
    int view_radius = static_cast<int>(view_radius_);

    // --- Phase 1: Determine which chunks should be loaded/simulated ---
    // We check loaded_radius + force_loaded chunks.
    std::unordered_set<ChunkPos, ChunkPosHash> should_be_loaded_set;

    // Player-proximity loaded chunks.
    for (int dy = -load_radius; dy <= load_radius; ++dy) {
        for (int dx = -load_radius; dx <= load_radius; ++dx) {
            if (dx * dx + dy * dy > load_radius * load_radius) continue;
            ChunkPos pos{player_layer_, player_cx_ + dx, player_cy_ + dy};
            should_be_loaded_set.insert(pos);
        }
    }

    // Force-loaded chunks always stay in the set.
    for (const auto& pos : force_loaded_chunks_) {
        should_be_loaded_set.insert(pos);
    }

    // --- Phase 2: Determine which should be visible ---
    std::unordered_set<ChunkPos, ChunkPosHash> should_be_visible_set;
    for (int dy = -view_radius; dy <= view_radius; ++dy) {
        for (int dx = -view_radius; dx <= view_radius; ++dx) {
            if (dx * dx + dy * dy > view_radius * view_radius) continue;
            ChunkPos pos{player_layer_, player_cx_ + dx, player_cy_ + dy};
            should_be_visible_set.insert(pos);
        }
    }

    // --- Phase 3: Unload chunks that are no longer needed ---
    // Remove views for chunks that should not be visible.
    std::vector<ChunkPos> to_hide;
    for (const auto& pair : visible_views_) {
        if (should_be_visible_set.find(pair.first) == should_be_visible_set.end()) {
            to_hide.push_back(pair.first);
        }
    }
    for (const auto& pos : to_hide) {
        hide_chunk(String(pos.layer.c_str()), pos.cx, pos.cy);
    }

    // Unload from WorldData chunks that are no longer needed (if enabled).
    if (unload_distant_chunks_) {
        std::vector<ChunkPos> to_unload_from_world;
        for (const auto& pos : tracked_loaded_chunks_) {
            if (should_be_loaded_set.find(pos) == should_be_loaded_set.end()) {
                to_unload_from_world.push_back(pos);
            }
        }
        for (const auto& pos : to_unload_from_world) {
            // Remove from WorldData entirely.
            gd_world->remove_chunk(
                String(pos.layer.c_str()), pos.cx, pos.cy);
            tracked_loaded_chunks_.erase(pos);
        }
    }

    // --- Phase 4: Ensure needed chunks exist in WorldData ---
    for (const auto& pos : should_be_loaded_set) {
        if (tracked_loaded_chunks_.find(pos) != tracked_loaded_chunks_.end()) {
            continue; // Already loaded.
        }

        String layer_str(pos.layer.c_str());
        if (gd_world->has_chunk(layer_str, pos.cx, pos.cy)) {
            tracked_loaded_chunks_.insert(pos);
        } else {
            // Request async generation.
            gd_world->request_chunk_async(layer_str, pos.cx, pos.cy);
        }
    }

    // --- Phase 5: Ensure visible chunks have views ---
    for (const auto& pos : should_be_visible_set) {
        if (visible_views_.find(pos) != visible_views_.end()) {
            continue; // Already has a view.
        }

        // Only create a view if the chunk data is ready.
        if (tracked_loaded_chunks_.find(pos) != tracked_loaded_chunks_.end()) {
            show_chunk(String(pos.layer.c_str()), pos.cx, pos.cy);
        }
    }
}

void GDChunkManager::ensure_chunk_loaded(const String& layer, int cx, int cy) {
    if (world_data_ == nullptr) return;

    auto* gd_world = Object::cast_to<GDWorldData>(world_data_);
    if (gd_world == nullptr) return;

    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};

    if (tracked_loaded_chunks_.find(pos) != tracked_loaded_chunks_.end()) {
        return; // Already tracked.
    }

    if (gd_world->has_chunk(layer, cx, cy)) {
        tracked_loaded_chunks_.insert(pos);
    } else {
        gd_world->request_chunk_async(layer, cx, cy);
    }
}

void GDChunkManager::show_chunk(const String& layer, int cx, int cy) {
    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};

    // Skip if already visible.
    if (visible_views_.find(pos) != visible_views_.end()) {
        return;
    }

    if (world_data_ == nullptr) return;

    auto* gd_world = Object::cast_to<GDWorldData>(world_data_);
    if (gd_world == nullptr) return;

    // Get terrain data from WorldData.
    Dictionary terrain = gd_world->get_chunk_terrain(layer, cx, cy);
    if (terrain.is_empty()) {
        // Data not ready yet — request async and return.
        gd_world->request_chunk_async(layer, cx, cy);
        return;
    }

    // Create the chunk view (TileMapLayer).
    GDChunkView* view = memnew(GDChunkView);
    view->set_layer_id(layer);
    view->set_chunk_x(cx);
    view->set_chunk_y(cy);

    // Position at world coordinates.
    position_chunk_view(view, cx, cy);

    // Parent to the correct layer container.
    Node* container = ensure_layer_container(layer_str);
    if (container != nullptr) {
        container->add_child(view);
    }

    // Render terrain tiles.
    view->update_from_terrain(terrain);

    // Track the view.
    visible_views_[pos] = view;

    emit_signal("chunk_shown", layer, cx, cy);
}

void GDChunkManager::hide_chunk(const String& layer, int cx, int cy) {
    std::string layer_str = layer.utf8().get_data();
    ChunkPos pos{layer_str, cx, cy};

    auto it = visible_views_.find(pos);
    if (it == visible_views_.end()) {
        return;
    }

    GDChunkView* view = it->second;
    if (view != nullptr) {
        view->queue_free();
    }

    visible_views_.erase(it);

    emit_signal("chunk_hidden", layer, cx, cy);
}

void GDChunkManager::unload_chunk(const String& layer, int cx, int cy) {
    hide_chunk(layer, cx, cy);

    if (world_data_ == nullptr) return;
    auto* gd_world = Object::cast_to<GDWorldData>(world_data_);
    if (gd_world != nullptr) {
        gd_world->remove_chunk(layer, cx, cy);
    }

    std::string layer_str = layer.utf8().get_data();
    tracked_loaded_chunks_.erase(ChunkPos{layer_str, cx, cy});

    emit_signal("chunk_unloaded", layer, cx, cy);
}

void GDChunkManager::unload_all_chunks() {
    // Collect all visible chunk positions.
    std::vector<ChunkPos> all_visible;
    for (const auto& pair : visible_views_) {
        all_visible.push_back(pair.first);
    }

    // Hide all views.
    for (const auto& pos : all_visible) {
        hide_chunk(String(pos.layer.c_str()), pos.cx, pos.cy);
    }

    // Clear WorldData if configured.
    if (unload_distant_chunks_ && world_data_ != nullptr) {
        auto* gd_world = Object::cast_to<GDWorldData>(world_data_);
        if (gd_world != nullptr) {
            gd_world->clear();
        }
    }

    tracked_loaded_chunks_.clear();
    force_loaded_chunks_.clear();
}

Array GDChunkManager::get_visible_chunks() const {
    Array result;
    for (const auto& pair : visible_views_) {
        Dictionary key;
        key["layer"] = String(pair.first.layer.c_str());
        key["chunk_x"] = pair.first.cx;
        key["chunk_y"] = pair.first.cy;
        result.append(key);
    }
    return result;
}

Array GDChunkManager::get_loaded_chunks() const {
    Array result;
    for (const auto& pos : tracked_loaded_chunks_) {
        Dictionary key;
        key["layer"] = String(pos.layer.c_str());
        key["chunk_x"] = pos.cx;
        key["chunk_y"] = pos.cy;
        result.append(key);
    }
    return result;
}

int64_t GDChunkManager::get_visible_chunk_count() const {
    return static_cast<int64_t>(visible_views_.size());
}

int64_t GDChunkManager::get_loaded_chunk_count() const {
    return static_cast<int64_t>(tracked_loaded_chunks_.size());
}

bool GDChunkManager::is_within_loaded_radius(const std::string& layer, int cx, int cy) const {
    if (layer != player_layer_) return false;
    int dx = cx - player_cx_;
    int dy = cy - player_cy_;
    return dx * dx + dy * dy <= loaded_radius_ * loaded_radius_;
}

bool GDChunkManager::is_within_view_radius(const std::string& layer, int cx, int cy) const {
    if (layer != player_layer_) return false;
    int dx = cx - player_cx_;
    int dy = cy - player_cy_;
    return dx * dx + dy * dy <= view_radius_ * view_radius_;
}

bool GDChunkManager::should_be_loaded(const ChunkPos& pos) const {
    if (force_loaded_chunks_.count(pos) > 0) return true;
    return is_within_loaded_radius(pos.layer, pos.cx, pos.cy);
}

bool GDChunkManager::should_be_visible(const ChunkPos& pos) const {
    return is_within_view_radius(pos.layer, pos.cx, pos.cy);
}

std::string GDChunkManager::layer_to_container_str(const std::string& layer) const {
    if (layer == "surface") return "surface";
    if (layer == "underground") return "underground";
    return layer;
}

Node* GDChunkManager::get_layer_container(const std::string& layer) const {
    NodePath path;
    if (layer == "surface") {
        path = surface_container_path_;
    } else if (layer == "underground") {
        path = underground_container_path_;
    } else {
        return nullptr;
    }

    if (path.is_empty()) {
        return nullptr;
    }

    return get_node_or_null(path);
}

Node* GDChunkManager::ensure_layer_container(const std::string& layer) {
    Node* container = get_layer_container(layer);
    if (container == nullptr) {
        String node_name(String(layer.c_str()) + "_chunks");
        container = get_node_or_null(NodePath(node_name));
        if (container == nullptr) {
            container = memnew(Node2D);
            container->set_name(node_name);
            add_child(container);
        }
    }
    return container;
}

void GDChunkManager::position_chunk_view(GDChunkView* view, int cx, int cy) {
    int tile_size = static_cast<int>(tile_size_);
    int chunk_pixels = tile_size * ChunkData::kChunkSize;
    view->set_position(Vector2(
        static_cast<float>(cx * chunk_pixels),
        static_cast<float>(cy * chunk_pixels)));
}

void GDChunkManager::_bind_methods() {
    // World data.
    ClassDB::bind_method(D_METHOD("set_world_data", "world"),
                         &GDChunkManager::set_world_data);
    ClassDB::bind_method(D_METHOD("get_world_data"),
                         &GDChunkManager::get_world_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_data",
                 PROPERTY_HINT_RESOURCE_TYPE, "GDWorldData"),
                 "set_world_data", "get_world_data");

    // Loaded radius (simulation keep-alive).
    ClassDB::bind_method(D_METHOD("set_loaded_radius", "radius"),
                         &GDChunkManager::set_loaded_radius);
    ClassDB::bind_method(D_METHOD("get_loaded_radius"),
                         &GDChunkManager::get_loaded_radius);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "loaded_radius"),
                 "set_loaded_radius", "get_loaded_radius");

    // View radius (visual rendering).
    ClassDB::bind_method(D_METHOD("set_view_radius", "radius"),
                         &GDChunkManager::set_view_radius);
    ClassDB::bind_method(D_METHOD("get_view_radius"),
                         &GDChunkManager::get_view_radius);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "view_radius"),
                 "set_view_radius", "get_view_radius");

    // Tile size.
    ClassDB::bind_method(D_METHOD("set_tile_size", "size"),
                         &GDChunkManager::set_tile_size);
    ClassDB::bind_method(D_METHOD("get_tile_size"),
                         &GDChunkManager::get_tile_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tile_size"),
                 "set_tile_size", "get_tile_size");

    // Container paths.
    ClassDB::bind_method(D_METHOD("set_surface_container_path", "path"),
                         &GDChunkManager::set_surface_container_path);
    ClassDB::bind_method(D_METHOD("get_surface_container_path"),
                         &GDChunkManager::get_surface_container_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "surface_container_path"),
                 "set_surface_container_path", "get_surface_container_path");

    ClassDB::bind_method(D_METHOD("set_underground_container_path", "path"),
                         &GDChunkManager::set_underground_container_path);
    ClassDB::bind_method(D_METHOD("get_underground_container_path"),
                         &GDChunkManager::get_underground_container_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "underground_container_path"),
                 "set_underground_container_path", "get_underground_container_path");

    // Unload distant chunks.
    ClassDB::bind_method(D_METHOD("set_unload_distant_chunks", "enable"),
                         &GDChunkManager::set_unload_distant_chunks);
    ClassDB::bind_method(D_METHOD("get_unload_distant_chunks"),
                         &GDChunkManager::get_unload_distant_chunks);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "unload_distant_chunks"),
                 "set_unload_distant_chunks", "get_unload_distant_chunks");

    // Player tracking.
    ClassDB::bind_method(D_METHOD("set_player_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::set_player_chunk);
    ClassDB::bind_method(D_METHOD("get_player_chunk"),
                         &GDChunkManager::get_player_chunk);

    // Force-loaded chunks.
    ClassDB::bind_method(D_METHOD("force_load_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::force_load_chunk);
    ClassDB::bind_method(D_METHOD("force_unload_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::force_unload_chunk);
    ClassDB::bind_method(D_METHOD("is_chunk_force_loaded", "layer", "cx", "cy"),
                         &GDChunkManager::is_chunk_force_loaded);
    ClassDB::bind_method(D_METHOD("get_force_loaded_chunks"),
                         &GDChunkManager::get_force_loaded_chunks);

    // Chunk management.
    ClassDB::bind_method(D_METHOD("on_chunk_ready", "layer", "cx", "cy"),
                         &GDChunkManager::on_chunk_ready);
    ClassDB::bind_method(D_METHOD("refresh_chunks"),
                         &GDChunkManager::refresh_chunks);
    ClassDB::bind_method(D_METHOD("ensure_chunk_loaded", "layer", "cx", "cy"),
                         &GDChunkManager::ensure_chunk_loaded);
    ClassDB::bind_method(D_METHOD("show_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::show_chunk);
    ClassDB::bind_method(D_METHOD("hide_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::hide_chunk);
    ClassDB::bind_method(D_METHOD("unload_chunk", "layer", "cx", "cy"),
                         &GDChunkManager::unload_chunk);
    ClassDB::bind_method(D_METHOD("unload_all_chunks"),
                         &GDChunkManager::unload_all_chunks);
    ClassDB::bind_method(D_METHOD("get_visible_chunks"),
                         &GDChunkManager::get_visible_chunks);
    ClassDB::bind_method(D_METHOD("get_loaded_chunks"),
                         &GDChunkManager::get_loaded_chunks);
    ClassDB::bind_method(D_METHOD("get_visible_chunk_count"),
                         &GDChunkManager::get_visible_chunk_count);
    ClassDB::bind_method(D_METHOD("get_loaded_chunk_count"),
                         &GDChunkManager::get_loaded_chunk_count);

    // Signals.
    ADD_SIGNAL(MethodInfo("chunk_shown",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));

    ADD_SIGNAL(MethodInfo("chunk_hidden",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));

    ADD_SIGNAL(MethodInfo("chunk_unloaded",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));
}

} // namespace science_and_theology
