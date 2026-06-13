#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/world_gen/world_gen_config.hpp"
#include "gd_world_gen_config.h"

namespace science_and_theology {

class GDTerrainContentRegistry : public godot::Resource {
    GDCLASS(GDTerrainContentRegistry, godot::Resource)

public:
    GDTerrainContentRegistry();
    ~GDTerrainContentRegistry() override;

    enum TerrainFlagConst {
        FLAG_WALKABLE = TF_WALKABLE,
        FLAG_SOLID = TF_SOLID,
        FLAG_LIQUID = TF_LIQUID,
        FLAG_MINEABLE = TF_MINEABLE,
    };

    bool register_material(const godot::Dictionary& def);
    bool register_tile_mapping(const godot::Dictionary& def);
    bool set_material_roles(const godot::Dictionary& def);
    bool register_base_terrain_rule(const godot::Dictionary& def);
    bool register_biome_rule(const godot::Dictionary& def);
    bool register_ore_vein_rule(const godot::Dictionary& def);
    godot::Ref<GDWorldGenConfig> freeze();
    godot::Array validate() const;
    void clear();

    bool is_frozen() const;
    int64_t get_material_id(const godot::String& key) const;
    godot::String get_material_key(int64_t id) const;
    int64_t get_material_count() const;
    int64_t get_tile_mapping_count() const;

protected:
    static void _bind_methods();

private:
    bool check_mutable(const char* operation) const;
    void rebuild_lookup();
    std::shared_ptr<WorldGenConfigSnapshot> build_snapshot() const;

    bool frozen_ = false;
    std::vector<TerrainMaterialDef> materials_;
    std::vector<TerrainTileMapping> tile_mappings_;
    TerrainMaterialRoles roles_;
    std::vector<BaseTerrainRule> base_terrain_rules_;
    std::vector<BiomeRule> biome_rules_;
    std::vector<OreVeinRule> ore_vein_rules_;
    std::unordered_map<std::string, TerrainMaterialId> material_ids_by_key_;
    std::unordered_map<int, std::string> material_keys_by_id_;
};

} // namespace science_and_theology
