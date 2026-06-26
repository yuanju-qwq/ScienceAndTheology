#include "ritual_recipe_registry.hpp"

#include "core/common/string_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<RitualRecipe> g_ritual_recipes;
static std::unordered_map<std::string, RitualRecipeId> g_ritual_id_map;

void RitualRecipeRegistry::reset() {
    // 清空所有全局变量（vector + map + 字符串池），不 reserve ID 0。
    g_ritual_recipes.clear();
    g_ritual_id_map.clear();
}

void RitualRecipeRegistry::initialize() {
    reset();

    // Reserve ID 0 as invalid.
    g_ritual_recipes.push_back({});

    // Built-in recipes are now registered from GDScript
    // (see BuiltinRitualRecipes.gd via GDRitualRecipeRegistry).
}

const RitualRecipe* RitualRecipeRegistry::get_by_id(RitualRecipeId id) {
    if (id == kInvalidRitualRecipeId || id >= g_ritual_recipes.size()) return nullptr;
    // 跳过空隙条目（显式 ID 留下的空 slot，id 为空）
    if (g_ritual_recipes[id].id == nullptr || g_ritual_recipes[id].id[0] == '\0') return nullptr;
    return &g_ritual_recipes[id];
}

const RitualRecipe* RitualRecipeRegistry::get_by_id_str(const char* id) {
    auto it = g_ritual_id_map.find(id);
    if (it == g_ritual_id_map.end()) return nullptr;
    return get_by_id(it->second);
}

size_t RitualRecipeRegistry::count() {
    return g_ritual_id_map.size();
}

RitualRecipeId RitualRecipeRegistry::register_recipe(const RitualRecipe& recipe,
                                                     RitualRecipeId explicit_id) {
    // 幂等：如果 recipe.id 已存在，直接返回已有 ID。
    auto it = g_ritual_id_map.find(recipe.id);
    if (it != g_ritual_id_map.end()) {
        return it->second;
    }

    // 强制显式 ID：不传 explicit_id 则拒绝注册（不再支持自动分配）
    if (explicit_id == kInvalidRitualRecipeId || explicit_id == 0) {
        return kInvalidRitualRecipeId;
    }
    RitualRecipeId id = explicit_id;

    // 显式 ID 可能跳跃，需要 resize 填补空隙
    if (id >= g_ritual_recipes.size()) {
        g_ritual_recipes.resize(static_cast<size_t>(id) + 1);
    }
    g_ritual_recipes[id] = recipe;
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
        // 跳过空隙条目（显式 ID 留下的空 slot）
        if (g_ritual_recipes[i].id == nullptr || g_ritual_recipes[i].id[0] == '\0') continue;
        if (matches_slots(g_ritual_recipes[i], pedestal_elements, pedestal_tiers)) {
            return &g_ritual_recipes[i];
        }
    }
    return nullptr;
}

void RitualRecipeRegistry::register_builtin_recipes() {
    // Built-in recipes are now registered from GDScript
    // (see BuiltinRitualRecipes.gd via GDRitualRecipeRegistry).
}

} // namespace science_and_theology::magic
