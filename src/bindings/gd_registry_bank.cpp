#include "gd_registry_bank.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "core/common/string_pool.hpp"
#include "core/material/material.hpp"
#include "core/material/material_registry.hpp"
#include "core/material/material_item.hpp"
#include "core/fluid/fluid_registry.hpp"
#include "core/fuel/fuel_registry.hpp"
#include "core/loot/loot_table_registry.hpp"
#include "core/machine/recipe.hpp"
#include "core/crafting/crafting.hpp"
#include "core/machine/machine_definition_registry.hpp"
#include "core/magic/rune_registry.hpp"
#include "core/magic/glyph_registry.hpp"
#include "core/magic/ritual_recipe_registry.hpp"
#include "core/source_law/elixir_registry.hpp"
#include "core/source_law/sublimation_path_registry.hpp"
#include "core/source_law/dropped_organ_registry.hpp"
#include "core/simulation/creature_species.hpp"
#include "core/simulation/ecosystem_system.hpp"

namespace science_and_theology {

using namespace godot;

void GDRegistryBank::_bind_methods() {
    ClassDB::bind_static_method("GDRegistryBank",
        D_METHOD("reset_all"),
        &GDRegistryBank::reset_all);
    ClassDB::bind_static_method("GDRegistryBank",
        D_METHOD("reset_one", "registry_name"),
        &GDRegistryBank::reset_one);
}

void GDRegistryBank::reset_all() {
    // 材料和物品（gt 命名空间）
    gt::MaterialRegistry::reset();
    gt::ItemRegistry::reset();

    // 流体和燃料（gt 命名空间）
    gt::FluidRegistry::reset();
    gt::FuelRegistry::reset();

    // 战利品表（gt 命名空间）
    gt::LootTableRegistry::reset();

    // 机器配方和合成配方
    gt::RecipeDatabase::reset();
    gt::CraftingManager::reset();
    gt::MachineDefinitionRegistry::reset();

    // 魔法系统（magic 命名空间）
    magic::RuneRegistry::reset();
    magic::GlyphRegistry::reset();
    magic::RitualRecipeRegistry::reset();

    // 源律系统（source_law 命名空间）
    source_law::ElixirRegistry::reset();
    source_law::SublimationPathRegistry::reset();
    source_law::DroppedOrganRegistry::reset();

    // 生态系统（science_and_theology 命名空间，直接调用）
    CreatureSpeciesRegistry::reset();
    EcosystemSystem::reset_biome_staging();

    // 清空共享字符串池（必须在所有 registry reset 之后）
    gt::clear_string_pool();
}

void GDRegistryBank::reset_one(const String& registry_name) {
    const String name = registry_name.to_lower();

    if (name == "material") {
        gt::MaterialRegistry::reset();
    } else if (name == "item") {
        gt::ItemRegistry::reset();
    } else if (name == "fluid") {
        gt::FluidRegistry::reset();
    } else if (name == "fuel") {
        gt::FuelRegistry::reset();
    } else if (name == "rune") {
        magic::RuneRegistry::reset();
    } else if (name == "glyph") {
        magic::GlyphRegistry::reset();
    } else if (name == "ritual_recipe") {
        magic::RitualRecipeRegistry::reset();
    } else if (name == "elixir") {
        source_law::ElixirRegistry::reset();
    } else if (name == "sublimation_path") {
        source_law::SublimationPathRegistry::reset();
    } else if (name == "dropped_organ") {
        source_law::DroppedOrganRegistry::reset();
    } else if (name == "species") {
        CreatureSpeciesRegistry::reset();
    } else if (name == "biome_override") {
        EcosystemSystem::reset_biome_staging();
    }
}

} // namespace science_and_theology
