#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "elixir_def.hpp"

namespace science_and_theology::source_law {

using ElixirId = uint16_t;
inline constexpr ElixirId kInvalidElixirId = 0xFFFF;

// ============================================================
// ElixirRegistry — registry for all elixir recipes
// ============================================================
class ElixirRegistry {
public:
    static void initialize();
    // 清空所有全局状态（不 reserve ID 0），用于测试或重新装载。
    static void reset();

    static const ElixirRecipe* get_by_id(ElixirId id);
    static const ElixirRecipe* get_by_name(const char* name);

    // Find initiation elixirs for a given path.
    static std::vector<ElixirId> find_initiation_elixirs(SublimationPath path);

    static size_t count();

    // Register a recipe from GDScript. The recipe's id is persisted
    // internally; callers must ensure title_key remains valid for the
    // recipe's lifetime (the GD binding handles this).
    // Requires explicit_id (不再支持自动分配).
    static ElixirId register_recipe(const ElixirRecipe& recipe,
                                    ElixirId explicit_id);

private:
    static void register_builtin_recipes();
};

} // namespace science_and_theology::source_law
