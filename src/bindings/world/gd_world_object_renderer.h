#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <unordered_map>
#include <vector>

namespace science_and_theology {

// C++ sink of WorldObjectRenderer.gd's MultiMesh management.
// Uses RenderingServer directly to submit MultiMesh instances without
// creating MultiMeshInstance3D Node3D children (design doc: 方案 B).
//
// Lifecycle:
//   1. setup(scenario, material) — bind to a World3D scenario and set the
//      vertex-color material applied as material_override on every instance.
//   2. register_model(model_key, baked_mesh) — create a MultiMesh RID + a
//      visual instance RID for one model_key. The mesh is shared across all
//      instances of the same model.
//   3. place_object(model_key, cell, world_position) — append one instance
//      transform to the model's MultiMesh.
//   4. remove_object(model_key, cell) — swap-pop the instance at cell.
//   5. clear_all() — free every RID owned by this renderer.
//
// The class is RefCounted: GDScript holds a reference and delegates all
// MultiMesh operations here. Per-object fallback (custom_meshes) stays in
// GDScript — it's rarely used and not a hot path.
class GDWorldObjectRenderer : public godot::RefCounted {
    GDCLASS(GDWorldObjectRenderer, godot::RefCounted)

public:
    GDWorldObjectRenderer();
    ~GDWorldObjectRenderer() override;

    // Bind to a World3D scenario RID and set the vertex-color material.
    // Call once after construction (and again after a dimension switch
    // followed by clear_all()).
    void setup(const godot::RID& scenario, const godot::Ref<godot::Material>& material);

    // Create a MultiMesh + visual instance for a model_key. The baked_mesh
    // provides the geometry; the material from setup() is applied as
    // material_override. Safe to call multiple times for the same key (no-op).
    void register_model(const godot::StringName& model_key,
                        const godot::Ref<godot::ArrayMesh>& baked_mesh);

    // Append one instance at world_position. cell is used as the key for
    // later removal. No-op when the cell is already placed.
    void place_object(const godot::StringName& model_key,
                      const godot::Vector3i& cell,
                      const godot::Vector3& world_position);

    // Swap-pop the instance at cell. The last instance's transform is moved
    // into the removed slot so instance_count can simply decrement.
    void remove_object(const godot::StringName& model_key,
                       const godot::Vector3i& cell);

    // Free every RID owned by this renderer. Safe to call multiple times.
    void clear_all();

    // Number of instances currently placed for a model_key. For debugging.
    int32_t get_instance_count(const godot::StringName& model_key) const;

protected:
    static void _bind_methods();

private:
    // Per-model MultiMesh state.
    struct ModelEntry {
        godot::RID multimesh_rid;
        godot::RID instance_rid;
        std::vector<godot::Vector3i> cells;          // instance index -> cell
        std::unordered_map<int64_t, int32_t> cell_to_index;  // cell hash -> index
    };

    godot::RenderingServer* rs_ = nullptr;
    godot::RID scenario_;
    godot::RID material_override_rid_;
    std::unordered_map<uint64_t, ModelEntry> models_;

    // Hash a Vector3i into a 64-bit key for cell_to_index lookups.
    static uint64_t hash_key(const godot::StringName& model_key);
    static int64_t cell_hash(const godot::Vector3i& cell);
};

} // namespace science_and_theology
