#include "gd_species_registry.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include "core/simulation/creature_species.hpp"

namespace science_and_theology {

using namespace godot;

void GDSpeciesRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDSpeciesRegistry",
        D_METHOD("register_species", "def"),
        &GDSpeciesRegistry::register_species);
}

bool GDSpeciesRegistry::register_species(const Dictionary& def) {
    String species_key = def.get("species_key", "");
    if (species_key.is_empty()) return false;

    String title_key = def.get("title_key", "");
    if (title_key.is_empty()) return false;

    Variant role_v = def.get("role", Variant());
    if (role_v.get_type() != Variant::INT) return false;

    String model_key = def.get("model_key", "");
    if (model_key.is_empty()) return false;

    CreatureSpeciesDef cpp_def;
    cpp_def.species_key = species_key.utf8().get_data();
    cpp_def.title_key = title_key.utf8().get_data();
    cpp_def.role = static_cast<CreatureRole>(static_cast<int>(role_v));
    cpp_def.model_key = model_key.utf8().get_data();

    // 支持显式确定性 ID（P1: 热重载后 ID 不漂移）
    cpp_def.species_id = static_cast<uint16_t>(
        static_cast<int>(def.get("species_id", 0)));

    cpp_def.move_speed = static_cast<float>(def.get("move_speed", 0.0));
    cpp_def.base_health = static_cast<float>(def.get("base_health", 1.0));
    cpp_def.flee_detection_radius = static_cast<float>(
        def.get("flee_detection_radius", 0.0));
    cpp_def.wander_radius = static_cast<float>(def.get("wander_radius", 0.0));
    cpp_def.model_scale = static_cast<float>(def.get("model_scale", 1.0));

    // Parse biomes array.
    Array biomes = def.get("biomes", Array());
    for (int i = 0; i < biomes.size(); ++i) {
        cpp_def.biomes.push_back(
            static_cast<uint8_t>(static_cast<int>(biomes[i])));
    }

    // Parse drops array.
    Array drops = def.get("drops", Array());
    for (int i = 0; i < drops.size(); ++i) {
        Dictionary drop_dict = drops[i];
        CreatureDropDef drop;
        drop.item_key = std::string(
            drop_dict.get("item_key", "").operator String().utf8().get_data());
        drop.chance = static_cast<float>(
            drop_dict.get("chance", 0.0));
        drop.min_count = static_cast<int>(
            drop_dict.get("min_count", 1));
        drop.max_count = static_cast<int>(
            drop_dict.get("max_count", 1));
        cpp_def.drops.push_back(drop);
    }

    return CreatureSpeciesRegistry::staging().register_species(cpp_def);
}

} // namespace science_and_theology
