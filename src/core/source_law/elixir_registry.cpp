#include "elixir_registry.hpp"

#include "core/common/string_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::source_law {

static std::vector<ElixirRecipe> g_elixir_recipes;
static std::unordered_map<std::string, ElixirId> g_elixir_name_map;

void ElixirRegistry::reset() {
    // 清空所有全局变量（vector + map + 字符串池），不 reserve ID 0。
    g_elixir_recipes.clear();
    g_elixir_name_map.clear();
}

void ElixirRegistry::initialize() {
    reset();

    // Reserve ID 0 as invalid.
    g_elixir_recipes.push_back({});

    // Built-in recipes are now registered from GDScript via
    // GDElixirRegistry (see BuiltinElixirs.gd).
}

const ElixirRecipe* ElixirRegistry::get_by_id(ElixirId id) {
    if (id == kInvalidElixirId || id >= g_elixir_recipes.size()) return nullptr;
    // 跳过空隙条目（显式 ID 留下的空 slot，id 为空）
    if (g_elixir_recipes[id].id == nullptr || g_elixir_recipes[id].id[0] == '\0') return nullptr;
    return &g_elixir_recipes[id];
}

const ElixirRecipe* ElixirRegistry::get_by_name(const char* name) {
    auto it = g_elixir_name_map.find(name);
    if (it == g_elixir_name_map.end()) return nullptr;
    return get_by_id(it->second);
}

std::vector<ElixirId> ElixirRegistry::find_initiation_elixirs(SublimationPath path) {
    std::vector<ElixirId> result;
    for (size_t i = 1; i < g_elixir_recipes.size(); ++i) {
        // 跳过空隙条目（显式 ID 留下的空 slot）
        if (g_elixir_recipes[i].id == nullptr || g_elixir_recipes[i].id[0] == '\0') continue;
        if (g_elixir_recipes[i].type == ElixirType::INITIATION &&
            g_elixir_recipes[i].target_path == path) {
            result.push_back(static_cast<ElixirId>(i));
        }
    }
    return result;
}

size_t ElixirRegistry::count() {
    return g_elixir_name_map.size();
}

ElixirId ElixirRegistry::register_recipe(const ElixirRecipe& recipe, ElixirId explicit_id) {
    // 幂等：如果 recipe.id 已存在，直接返回已有 ID。
    auto it = g_elixir_name_map.find(recipe.id);
    if (it != g_elixir_name_map.end()) {
        return it->second;
    }

    // 强制显式 ID：不传 explicit_id 则拒绝注册（不再支持自动分配）
    if (explicit_id == kInvalidElixirId || explicit_id == 0) {
        return kInvalidElixirId;
    }
    ElixirId id = explicit_id;

    // 显式 ID 可能跳跃，需要 resize 填补空隙
    if (id >= g_elixir_recipes.size()) {
        g_elixir_recipes.resize(static_cast<size_t>(id) + 1);
    }
    g_elixir_recipes[id] = recipe;
    g_elixir_name_map[recipe.id] = id;
    return id;
}

void ElixirRegistry::register_builtin_recipes() {
    // Migrated to GDScript (see scripts/content/BuiltinElixirs.gd).
    // Retained as a no-op for ABI compatibility.
}

} // namespace science_and_theology::source_law
