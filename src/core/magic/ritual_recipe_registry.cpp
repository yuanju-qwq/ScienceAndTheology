#include "ritual_recipe_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<RitualRecipe> g_ritual_recipes;
static std::vector<std::string> g_ritual_name_storage;
static std::vector<std::string> g_ritual_display_name_storage;
static std::unordered_map<std::string, RitualRecipeId> g_ritual_id_map;

void RitualRecipeRegistry::initialize() {
    g_ritual_recipes.clear();
    g_ritual_name_storage.clear();
    g_ritual_display_name_storage.clear();
    g_ritual_id_map.clear();

    // Reserve ID 0 as invalid.
    g_ritual_recipes.push_back({});
    g_ritual_name_storage.push_back("__invalid__");
    g_ritual_display_name_storage.push_back("__invalid__");

    register_builtin_recipes();
}

const RitualRecipe* RitualRecipeRegistry::get_by_id(RitualRecipeId id) {
    if (id == kInvalidRitualRecipeId || id >= g_ritual_recipes.size()) return nullptr;
    return &g_ritual_recipes[id];
}

const RitualRecipe* RitualRecipeRegistry::get_by_id_str(const char* id) {
    auto it = g_ritual_id_map.find(id);
    if (it == g_ritual_id_map.end()) return nullptr;
    return get_by_id(it->second);
}

size_t RitualRecipeRegistry::count() {
    return g_ritual_recipes.size() > 0 ? g_ritual_recipes.size() - 1 : 0;
}

RitualRecipeId RitualRecipeRegistry::register_recipe(const RitualRecipe& recipe) {
    RitualRecipeId id = static_cast<RitualRecipeId>(g_ritual_recipes.size());

    g_ritual_name_storage.push_back(recipe.id);
    g_ritual_display_name_storage.push_back(recipe.display_name);

    RitualRecipe stored = recipe;
    stored.id = g_ritual_name_storage.back().c_str();
    stored.display_name = g_ritual_display_name_storage.back().c_str();

    g_ritual_recipes.push_back(stored);
    g_ritual_id_map[recipe.id] = id;
    return id;
}

bool RitualRecipeRegistry::matches_slots(
        const RitualRecipe& recipe,
        const std::vector<RuneElement>& elements,
        const std::vector<RuneTier>& tiers) {

    size_t count = elements.size();
    if (count != tiers.size()) return false;
    if (count < recipe.pedestals.size()) return false;

    for (size_t i = 0; i < recipe.pedestals.size(); ++i) {
        const auto& slot = recipe.pedestals[i];

        if (slot.strict_element && elements[i] != slot.element) {
            return false;
        }

        if (tiers[i] < slot.min_tier) {
            return false;
        }
    }

    return true;
}

const RitualRecipe* RitualRecipeRegistry::match(
        const std::vector<RuneElement>& pedestal_elements,
        const std::vector<RuneTier>& pedestal_tiers) {

    for (size_t i = 1; i < g_ritual_recipes.size(); ++i) {
        if (matches_slots(g_ritual_recipes[i], pedestal_elements, pedestal_tiers)) {
            return &g_ritual_recipes[i];
        }
    }
    return nullptr;
}

void RitualRecipeRegistry::register_builtin_recipes() {
    // --- Machine blessings ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::FIRE,   RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::FIRE,   RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_machine_speed",
            "Machine Speed I",
            r.pedestals,
            40, 100, false,
            {RitualEffectType::MACHINE_BLESSING, R"({"boost":"speed","mult":1.2})", 72000}
        });
    }

    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::WATER,  RuneTier::COMMON, true},
            {RuneElement::WATER,  RuneTier::COMMON, true},
            {RuneElement::WATER,  RuneTier::COMMON, true},
            {RuneElement::WATER,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_machine_cooling",
            "Machine Cooling",
            r.pedestals,
            50, 120, false,
            {RitualEffectType::MACHINE_BLESSING, R"({"boost":"no_maintenance"})", 144000}
        });
    }

    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::EARTH,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::LIGHT,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_machine_boost",
            "Machine Output I",
            r.pedestals,
            55, 100, false,
            {RitualEffectType::MACHINE_BLESSING, R"({"boost":"output","mult":1.15})", 72000}
        });
    }

    // --- World events ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::LIGHT,  RuneTier::COMMON, true},
            {RuneElement::LIGHT,  RuneTier::COMMON, true},
            {RuneElement::DARK,   RuneTier::COMMON, true},
            {RuneElement::DARK,   RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_toggle_ruin_gate",
            "Toggle Ruin Gate",
            r.pedestals,
            30, 80, true,
            {RitualEffectType::WORLD_EVENT, R"({"event":"toggle_ruin_gate"})", 0}
        });
    }

    // --- Player buffs ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::AIR,    RuneTier::COMMON, true},
            {RuneElement::AIR,    RuneTier::COMMON, true},
            {RuneElement::AIR,    RuneTier::COMMON, true},
            {RuneElement::AIR,    RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_player_speed",
            "Player Speed Boost",
            r.pedestals,
            25, 100, true,
            {RitualEffectType::PLAYER_BUFF, R"({"buff":"speed","mult":1.5})", 36000}
        });
    }

    // --- Chaos combo ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::CHAOS,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::CHAOS,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_machine_double",
            "Machine Random Double Output",
            r.pedestals,
            70, 150, true,
            {RitualEffectType::MACHINE_BLESSING, R"({"boost":"random_double","chance":0.2})", 36000}
        });
    }

    // --- Teleportation ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::FIRE,   RuneTier::COMMON, true},
            {RuneElement::EARTH,  RuneTier::COMMON, true},
            {RuneElement::WATER,  RuneTier::COMMON, true},
            {RuneElement::AIR,    RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_teleport_link",
            "Altar Teleport Link",
            r.pedestals,
            80, 200, true,
            {RitualEffectType::TELEPORTATION, R"({})", 0}
        });
    }

    // --- Divination ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::LIGHT,  RuneTier::COMMON, true},
            {RuneElement::EARTH,  RuneTier::COMMON, true},
            {RuneElement::LIGHT,  RuneTier::COMMON, true},
            {RuneElement::EARTH,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_divination",
            "Divination",
            r.pedestals,
            20, 60, false,
            {RitualEffectType::DIVINATION, R"({})", 0}
        });
    }

    // --- Mana expansion ---
    {
        RitualRecipe r;
        r.pedestals = {
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true},
            {RuneElement::ORDER,  RuneTier::COMMON, true}
        };
        register_recipe(RitualRecipe{
            "ritual_mana_expand",
            "Mana Expansion +25",
            r.pedestals,
            60, 150, true,
            {RitualEffectType::MANA_EXPANSION, R"({"amount":25})", 0}
        });
    }
}

} // namespace science_and_theology::magic
