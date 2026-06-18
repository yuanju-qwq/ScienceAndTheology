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

    static const ElixirRecipe* get_by_id(ElixirId id);
    static const ElixirRecipe* get_by_name(const char* name);

    // Find initiation elixirs for a given path.
    static std::vector<ElixirId> find_initiation_elixirs(SublimationPath path);

    static size_t count();

private:
    static ElixirId register_recipe(const ElixirRecipe& recipe);
    static void register_builtin_recipes();
};

} // namespace science_and_theology::source_law
