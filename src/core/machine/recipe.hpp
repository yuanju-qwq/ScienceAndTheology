#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/resource_key.hpp"
#include "../config/gt_values.hpp"

namespace science_and_theology::gt {

// ============================================================
// Recipe input/output types
// ============================================================

// A single input requirement for a recipe.
// Uses ResourceStack to support both item and fluid inputs.
using RecipeInput = ResourceStack;

// A single output product of a recipe.
struct RecipeOutput {
    ResourceStack stack;               // produced resource
    float probability = 1.0f;          // 1.0 = always, 0.15 = 15% chance

    RecipeOutput() = default;
    RecipeOutput(ResourceStack s, float p = 1.0f)
        : stack(std::move(s)), probability(p) {}

    bool is_guaranteed() const { return probability >= 1.0f; }
    bool is_valid() const {
        return stack.is_valid() && probability > 0.0f;
    }

    ItemId item_id() const { return stack.item_id(); }
    int64_t count() const { return stack.amount; }
};

// ============================================================
// Recipe definition
// ============================================================

// A complete processing recipe.
// Mirrors GT5's recipe system: each machine type has its own recipe map.
//
// Usage:
//   auto recipe = RecipeBuilder("centrifuge")
//       .name("centrifuge_iron_dust")
//       .input(ResourceStack::item(iron_dust_id, 1))
//       .output(ResourceStack::item(tiny_iron_dust_id, 6))
//       .chanced_output(ResourceStack::item(gold_nugget_id, 1), 0.08f)
//       .fluid_input(ResourceStack::fluid(water_id, 1000))
//       .duration_ticks(400)
//       .eu_per_tick(5)
//       .tier(VoltageTier::LV)
//       .build();
struct Recipe {
    const char* name = "";              // unique recipe identifier
    const char* machine_type = "";      // e.g. "centrifuge", "blast_furnace"
    const char* category = "";          // grouping tag, e.g. "ore_processing"

    VoltageTier min_tier = VoltageTier::ULV;  // minimum voltage tier
    int64_t eu_per_tick = 0;           // base energy per tick
    int64_t duration_ticks = 0;        // base processing time (1 tick = 1/20 sec at 20 TPS)
    int64_t total_eu() const {         // total energy without overclock
        return eu_per_tick * duration_ticks;
    }

    std::vector<ResourceStack> inputs;  // both items and fluids
    std::vector<RecipeOutput> outputs;

    // Returns true if this recipe matches a given set of input resources.
    bool matches_inputs(const std::vector<ResourceStack>& candidates) const;

    // Returns true if this recipe can run at the given voltage tier.
    bool is_voltage_sufficient(VoltageTier tier) const {
        return tier >= min_tier;
    }

    // Returns whether this recipe has any chanced (non-guaranteed) outputs.
    bool has_chanced_outputs() const;
};

// ============================================================
// Recipe builder (fluent API)
// ============================================================

class RecipeBuilder {
public:
    explicit RecipeBuilder(const char* machine_type);

    RecipeBuilder& name(const char* recipe_name);
    RecipeBuilder& category(const char* cat);

    // Item input (convenience: just ID + count).
    RecipeBuilder& input(ItemId item, int64_t count);

    // Generic input (supports both items and fluids).
    RecipeBuilder& input(const ResourceStack& stack);

    // Fluid input convenience.
    RecipeBuilder& fluid_input(FluidId fluid, int64_t mb);

    // Output types.
    RecipeBuilder& output(const ResourceStack& stack);
    RecipeBuilder& output(ItemId item, int64_t count);
    RecipeBuilder& fluid_output(FluidId fluid, int64_t mb);

    // Chanced byproduct output.
    RecipeBuilder& chanced_output(const ResourceStack& stack, float probability);
    RecipeBuilder& chanced_output(ItemId item, int64_t count, float probability);

    RecipeBuilder& duration_ticks(int64_t ticks);
    RecipeBuilder& eu_per_tick(int64_t eu);
    RecipeBuilder& tier(VoltageTier min_tier);

    // Convenience: set duration in seconds (at 20 TPS).
    RecipeBuilder& duration_seconds(float seconds);

    Recipe build() const;

private:
    Recipe recipe_;
};

// ============================================================
// Recipe map — organizes recipes by machine type
// ============================================================

// A collection of recipes associated with a specific machine type.
// Provides lookup by inputs to find matching recipes.
class RecipeMap {
public:
    explicit RecipeMap(const char* machine_type);

    const char* machine_type() const { return machine_type_.c_str(); }

    // Add a recipe to this map.
    void add(const Recipe& recipe);
    void add(Recipe&& recipe);

    // Returns all recipes registered in this map.
    const std::vector<Recipe>& recipes() const { return recipes_; }
    size_t recipe_count() const { return recipes_.size(); }

    // Find recipes whose inputs are fully satisfied by the given resources.
    // If multiple recipes match, returns all of them.
    std::vector<const Recipe*> find_matching(
            const std::vector<ResourceStack>& items) const;

    // Find the first recipe that matches. Returns nullptr if none.
    const Recipe* find_first_matching(
            const std::vector<ResourceStack>& items) const;

    // Find a recipe by name. Returns nullptr if not found.
    const Recipe* find_by_name(const char* name) const;

private:
    std::string machine_type_;
    std::vector<Recipe> recipes_;
};

// ============================================================
// Global recipe database
// ============================================================

// Central registry of all RecipeMaps, one per machine type.
class RecipeDatabase {
public:
    static void initialize();
    static void reset();

    // Get or create a RecipeMap for a given machine type.
    static RecipeMap* get_map(const char* machine_type);

    // Find an existing RecipeMap. Returns nullptr without creating a map.
    static RecipeMap* find_map(const char* machine_type);

    // Internal: get or create a map. Used by add_recipe and register_*.
    static RecipeMap* find_or_create_map(const char* machine_type);

    // Add a single recipe to the appropriate map.
    static void add_recipe(const Recipe& recipe);

    // Returns all registered machine types.
    static std::vector<const char*> get_machine_types();

    // Returns total recipe count across all maps.
    static size_t get_total_recipe_count();

    // Pre-register all built-in recipes.
    static void register_builtin_recipes();

    // Register recipes for a specific machine type.
    static void register_furnace_recipes();
    static void register_macerator_recipes();
    static void register_compressor_recipes();
    static void register_centrifuge_recipes();
    static void register_electrolyzer_recipes();
    static void register_fluid_solidifier_recipes();
    static void register_assembler_recipes();

private:
    static std::vector<std::unique_ptr<RecipeMap>>& maps();
};

} // namespace science_and_theology::gt
