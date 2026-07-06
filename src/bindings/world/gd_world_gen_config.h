#pragma once

#include <memory>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/world_gen/world_gen_config.hpp"

namespace science_and_theology {

class GDWorldGenConfig : public godot::Resource {
    GDCLASS(GDWorldGenConfig, godot::Resource)

public:
    GDWorldGenConfig();
    ~GDWorldGenConfig() override;

    void set_snapshot(std::shared_ptr<const WorldGenConfigSnapshot> snapshot);
    std::shared_ptr<const WorldGenConfigSnapshot> get_snapshot() const;

    int64_t get_schema_version() const;
    int64_t get_content_hash() const;
    int64_t get_material_count() const;
    int64_t get_material_visual_count() const;
    int64_t get_material_id(const godot::String& key) const;
    godot::String get_material_key(int64_t id) const;
    godot::Dictionary get_material_def(int64_t id) const;
    godot::Array get_material_defs() const;
    godot::Array get_material_visuals() const;
    godot::Dictionary get_material_roles() const;
    godot::Dictionary get_runtime_material_ids() const;

    // --- Derived data for chunk rendering (sinks of GDScript logic) ---

    // Build a 256-byte collidable material mask: mask[id]=1 when the material
    // has TF_WALKABLE or TF_SOLID flag. Consumed by GDChunkHelper::
    // build_collision_faces as collidable_material_mask.
    godot::PackedByteArray build_collidable_material_mask() const;

    // Resolve runtime material IDs (sinks of ChunkRendererBridge.
    // _resolve_runtime_material_ids). Returns 0 (AIR) when not set.
    int32_t get_ladder_material_id() const;
    int32_t get_workbench_material_id() const;

    // Validate a requested section_size against chunk dimensions. Returns the
    // usable section_size, or 0 when sectioning is disabled (size <= 0, larger
    // than the smallest dimension, or does not evenly divide all dimensions).
    int32_t get_mesh_section_size(int32_t size_x, int32_t size_y, int32_t size_z,
                                  int32_t requested) const;

    godot::Array get_base_terrain_rules() const;
    godot::Array get_biome_rules() const;
    godot::Array get_ore_vein_groups() const;
    godot::Array get_rock_layer_rules() const;
    godot::Array validate() const;

protected:
    static void _bind_methods();

private:
    std::shared_ptr<const WorldGenConfigSnapshot> snapshot_;
};

} // namespace science_and_theology
