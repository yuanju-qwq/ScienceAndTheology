#pragma once

#include <memory>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
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
    int64_t get_tile_mapping_count() const;
    int64_t get_material_id(const godot::String& key) const;
    godot::String get_material_key(int64_t id) const;
    godot::Dictionary get_material_def(int64_t id) const;
    godot::Array get_material_defs() const;
    godot::Array get_tile_mappings() const;
    godot::Dictionary get_material_roles() const;
    godot::Dictionary get_runtime_material_ids() const;
    godot::Array get_base_terrain_rules() const;
    godot::Array get_biome_rules() const;
    godot::Array get_ore_vein_rules() const;
    godot::Array validate() const;

protected:
    static void _bind_methods();

private:
    std::shared_ptr<const WorldGenConfigSnapshot> snapshot_;
};

} // namespace science_and_theology
