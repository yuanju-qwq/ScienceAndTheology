#include "gd_world_object_renderer.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

GDWorldObjectRenderer::GDWorldObjectRenderer() {
    rs_ = godot::RenderingServer::get_singleton();
}

GDWorldObjectRenderer::~GDWorldObjectRenderer() {
    clear_all();
}

void GDWorldObjectRenderer::setup(const godot::RID& scenario,
                                   const godot::Ref<godot::Material>& material) {
    scenario_ = scenario;
    material_override_rid_ = material.is_valid() ? material->get_rid() : godot::RID();
}

void GDWorldObjectRenderer::register_model(const godot::StringName& model_key,
                                            const godot::Ref<godot::ArrayMesh>& baked_mesh) {
    if (rs_ == nullptr || scenario_.is_valid() == false) return;
    const uint64_t key_hash = model_key.hash();
    if (models_.find(key_hash) != models_.end()) return;  // already registered
    if (baked_mesh.is_null()) return;

    ModelEntry entry;
    entry.multimesh_rid = rs_->multimesh_create();
    rs_->multimesh_set_mesh(entry.multimesh_rid, baked_mesh->get_rid());
    rs_->multimesh_allocate_data(entry.multimesh_rid, 0,
        godot::RenderingServer::MULTIMESH_TRANSFORM_3D, false, false, false);

    entry.instance_rid = rs_->instance_create2(entry.multimesh_rid, scenario_);
    rs_->instance_set_visible(entry.instance_rid, true);
    if (material_override_rid_.is_valid()) {
        rs_->instance_geometry_set_material_override(entry.instance_rid, material_override_rid_);
    }

    models_[key_hash] = std::move(entry);
}

void GDWorldObjectRenderer::place_object(const godot::StringName& model_key,
                                          const godot::Vector3i& cell,
                                          const godot::Vector3& world_position) {
    if (rs_ == nullptr) return;
    const uint64_t key_hash = model_key.hash();
    auto it = models_.find(key_hash);
    if (it == models_.end()) return;
    ModelEntry& entry = it->second;
    const int64_t ch = cell_hash(cell);
    if (entry.cell_to_index.find(ch) != entry.cell_to_index.end()) return;  // already placed

    const int32_t idx = static_cast<int32_t>(entry.cells.size());
    entry.cells.push_back(cell);
    entry.cell_to_index[ch] = idx;

    rs_->multimesh_allocate_data(entry.multimesh_rid, idx + 1,
        godot::RenderingServer::MULTIMESH_TRANSFORM_3D, false, false, false);
    rs_->multimesh_instance_set_transform(entry.multimesh_rid, idx,
        godot::Transform3D(godot::Basis(), world_position));
}

void GDWorldObjectRenderer::remove_object(const godot::StringName& model_key,
                                           const godot::Vector3i& cell) {
    if (rs_ == nullptr) return;
    const uint64_t key_hash = model_key.hash();
    auto it = models_.find(key_hash);
    if (it == models_.end()) return;
    ModelEntry& entry = it->second;
    const int64_t ch = cell_hash(cell);
    auto idx_it = entry.cell_to_index.find(ch);
    if (idx_it == entry.cell_to_index.end()) return;

    const int32_t idx = idx_it->second;
    const int32_t last_idx = static_cast<int32_t>(entry.cells.size()) - 1;
    if (idx != last_idx) {
        const godot::Transform3D last_transform =
            rs_->multimesh_instance_get_transform(entry.multimesh_rid, last_idx);
        rs_->multimesh_instance_set_transform(entry.multimesh_rid, idx, last_transform);
        const godot::Vector3i last_cell = entry.cells[last_idx];
        entry.cells[idx] = last_cell;
        entry.cell_to_index[cell_hash(last_cell)] = idx;
    }
    entry.cells.pop_back();
    entry.cell_to_index.erase(ch);
    rs_->multimesh_allocate_data(entry.multimesh_rid,
        static_cast<int32_t>(entry.cells.size()),
        godot::RenderingServer::MULTIMESH_TRANSFORM_3D, false, false, false);
}

void GDWorldObjectRenderer::clear_all() {
    if (rs_ == nullptr) {
        models_.clear();
        return;
    }
    for (auto& [key, entry] : models_) {
        if (entry.instance_rid.is_valid()) {
            rs_->free_rid(entry.instance_rid);
            entry.instance_rid = godot::RID();
        }
        if (entry.multimesh_rid.is_valid()) {
            rs_->free_rid(entry.multimesh_rid);
            entry.multimesh_rid = godot::RID();
        }
        entry.cells.clear();
        entry.cell_to_index.clear();
    }
    models_.clear();
}

int32_t GDWorldObjectRenderer::get_instance_count(const godot::StringName& model_key) const {
    const uint64_t key_hash = model_key.hash();
    auto it = models_.find(key_hash);
    if (it == models_.end()) return 0;
    return static_cast<int32_t>(it->second.cells.size());
}

int64_t GDWorldObjectRenderer::cell_hash(const godot::Vector3i& cell) {
    // Pack a Vector3i into a 64-bit key for hash map lookups.
    return (static_cast<int64_t>(cell.z) << 40)
         | (static_cast<int64_t>(cell.y) << 20)
         | static_cast<int64_t>(cell.x);
}

uint64_t GDWorldObjectRenderer::hash_key(const godot::StringName& model_key) {
    return model_key.hash();
}

void GDWorldObjectRenderer::_bind_methods() {
    using B = GDWorldObjectRenderer;
    godot::ClassDB::bind_method(godot::D_METHOD("setup", "scenario", "material"), &B::setup);
    godot::ClassDB::bind_method(godot::D_METHOD("register_model", "model_key", "baked_mesh"), &B::register_model);
    godot::ClassDB::bind_method(godot::D_METHOD("place_object", "model_key", "cell", "world_position"), &B::place_object);
    godot::ClassDB::bind_method(godot::D_METHOD("remove_object", "model_key", "cell"), &B::remove_object);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_all"), &B::clear_all);
    godot::ClassDB::bind_method(godot::D_METHOD("get_instance_count", "model_key"), &B::get_instance_count);
}

} // namespace science_and_theology
