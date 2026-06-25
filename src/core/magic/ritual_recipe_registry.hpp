#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ritual_recipe.hpp"

namespace science_and_theology::magic {

using RitualRecipeId = uint8_t;
inline constexpr RitualRecipeId kInvalidRitualRecipeId = 0xFF;

class RitualRecipeRegistry {
public:
    static void initialize();

    static const RitualRecipe* get_by_id(RitualRecipeId id);
    static const RitualRecipe* get_by_id_str(const char* id);

    // Match pedestal configuration to a recipe.
    // pedestal_elements: element placed at each pedestal index
    // pedestal_tiers: tier placed at each pedestal index
    static const RitualRecipe* match(
        const std::vector<RuneElement>& pedestal_elements,
        const std::vector<RuneTier>& pedestal_tiers);

    static size_t count();

    // Register a recipe from GDScript. The recipe's id/title_key/param_json
    // strings are persisted internally so the returned RitualRecipe pointers
    // remain valid after registration.
    static RitualRecipeId register_recipe(const RitualRecipe& recipe);

private:
    static void register_builtin_recipes();
    static bool matches_slots(const RitualRecipe& recipe,
                              const std::vector<RuneElement>& elements,
                              const std::vector<RuneTier>& tiers);
};

} // namespace science_and_theology::magic
