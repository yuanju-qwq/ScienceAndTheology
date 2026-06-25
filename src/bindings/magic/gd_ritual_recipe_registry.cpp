#include "gd_ritual_recipe_registry.hpp"

#include <string>

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

namespace science_and_theology {

using namespace magic;

void GDRitualRecipeRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDRitualRecipeRegistry",
        D_METHOD("register_recipe", "def"),
        &GDRitualRecipeRegistry::register_recipe);
}

bool GDRitualRecipeRegistry::register_recipe(const Dictionary& def) {
    String id_str = def.get("id", "");
    if (id_str.is_empty()) return false;

    // Persist strings in std::string locals so the const char* pointers
    // remain valid for the RitualRecipeRegistry::register_recipe call.
    // RitualRecipeRegistry internally copies id/title_key into its own
    // storage, and RitualEffectData::param_json is a std::string (owned).
    std::string id_std = id_str.utf8().get_data();
    std::string title_std = String(def.get("title_key", "")).utf8().get_data();

    RitualRecipe recipe;
    recipe.id = id_std.c_str();
    recipe.title_key = title_std.c_str();

    // Pedestals.
    Array pedestals_arr = def.get("pedestals", Array());
    for (int i = 0; i < pedestals_arr.size(); ++i) {
        Dictionary slot_dict = pedestals_arr[i];
        RitualPedestalSlot slot;
        slot.element = static_cast<RuneElement>(
            static_cast<int>(slot_dict.get("element", 0)));
        slot.min_tier = static_cast<RuneTier>(
            static_cast<int>(slot_dict.get("min_tier", 0)));
        slot.strict_element = (bool)slot_dict.get("strict_element", false);
        recipe.pedestals.push_back(slot);
    }

    recipe.mana_cost = static_cast<int>(def.get("mana_cost", 50));
    recipe.duration_ticks = static_cast<int>(def.get("duration_ticks", 100));
    recipe.consume_runes = (bool)def.get("consume_runes", true);

    // Effect.
    Dictionary effect_dict = def.get("effect", Dictionary());
    recipe.effect.type = static_cast<RitualEffectType>(
        static_cast<int>(effect_dict.get("type", 0)));
    String param_json_str = effect_dict.get("param_json", "");
    recipe.effect.param_json = param_json_str.utf8().get_data();
    recipe.effect.duration_ticks =
        static_cast<int>(effect_dict.get("duration_ticks", 0));

    // 支持显式确定性 ID（P1: 热重载后 ID 不漂移）
    RitualRecipeId explicit_id = kInvalidRitualRecipeId;
    Variant eid_var = def.get("explicit_id", Variant());
    if (eid_var.get_type() == Variant::INT) {
        explicit_id = static_cast<RitualRecipeId>(static_cast<int>(eid_var));
    }

    RitualRecipeId rid = RitualRecipeRegistry::register_recipe(recipe, explicit_id);
    return rid != kInvalidRitualRecipeId;
}

} // namespace science_and_theology
