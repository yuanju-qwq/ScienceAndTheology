#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../material/item.hpp"
#include "../power/gt_values.hpp"

namespace science_and_theology::gt {

// ============================================================
// Recipe input/output types
// ============================================================

// Describes a single input requirement for a recipe.
struct RecipeInput {
    ItemId item_id = kInvalidItemId;  // required item
    int64_t count = 1;                // required amount

    bool is_valid() const { return item_id != kInvalidItemId && count > 0; }
};

// Describes a single output product of a recipe.
struct RecipeOutput {
    ItemId item_id = kInvalidItemId;  // produced item
    int64_t count = 1;                // amount produced
    float probability = 1.0f;         // 1.0 = always, 0.15 = 15% chance

    bool is_guaranteed() const { return probability >= 1.0f; }
    bool is_valid() const { return item_id != kInvalidItemId && count > 0 && probability > 0.0f; }
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
//       .input(iron_dust_id, 1)
//       .output(tiny_iron_dust_id, 6)
//       .chanced_output(gold_nugget_id, 1, 0.08f)
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

    std::vector<RecipeInput> inputs;
    std::vector<RecipeOutput> outputs;

    // Returns true if this recipe matches a given set of input items.
    // The candidate stack must satisfy all required inputs.
    bool matches_inputs(const std::vector<ItemStack>& candidates) const;

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
    RecipeBuilder& input(ItemId item, int64_t count);
    RecipeBuilder& output(ItemId item, int64_t count);
    // Chanced byproduct output.
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

    // Find recipes whose inputs are fully satisfied by the given items.
    // If multiple recipes match, returns all of them.
    std::vector<const Recipe*> find_matching(const std::vector<ItemStack>& items) const;

    // Find the first recipe that matches. Returns nullptr if none.
    const Recipe* find_first_matching(const std::vector<ItemStack>& items) const;

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

    // Get or create a RecipeMap for a given machine type.
    static RecipeMap* get_map(const char* machine_type);

    // Convenience: add a recipe directly.
    static void add_recipe(const Recipe& recipe);

    // Returns all registered machine types.
    static std::vector<const char*> get_machine_types();

    // Returns total recipe count across all maps.
    static size_t get_total_recipe_count();

private:
    static RecipeMap* find_or_create_map(const char* machine_type);
};

} // namespace science_and_theology::gt
