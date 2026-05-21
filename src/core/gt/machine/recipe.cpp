#include "recipe.hpp"

#include <cassert>
#include <cstring>
#include <unordered_map>
#include <string>

namespace science_and_theology::gt {

// ============================================================
// Recipe input matching
// ============================================================

bool Recipe::matches_inputs(const std::vector<ItemStack>& candidates) const {
    // Build a map from candidate items for quick lookup.
    // item_id -> total available count.
    std::unordered_map<ItemId, int64_t> available;
    for (const auto& stack : candidates) {
        if (stack.is_valid()) {
            available[stack.item_id] += stack.count;
        }
    }

    // Check each required input against available items.
    for (const auto& input : inputs) {
        if (!input.is_valid()) continue;

        auto it = available.find(input.item_id);
        if (it == available.end()) return false;       // item not found
        if (it->second < input.count) return false;    // not enough
        it->second -= input.count;                     // consume
    }

    return true;
}

bool Recipe::has_chanced_outputs() const {
    for (const auto& out : outputs) {
        if (!out.is_guaranteed()) return true;
    }
    return false;
}

// ============================================================
// RecipeBuilder
// ============================================================

RecipeBuilder::RecipeBuilder(const char* machine_type) {
    recipe_.machine_type = machine_type;
}

RecipeBuilder& RecipeBuilder::name(const char* recipe_name) {
    recipe_.name = recipe_name;
    return *this;
}

RecipeBuilder& RecipeBuilder::category(const char* cat) {
    recipe_.category = cat;
    return *this;
}

RecipeBuilder& RecipeBuilder::input(ItemId item, int64_t count) {
    recipe_.inputs.push_back(RecipeInput{item, count});
    return *this;
}

RecipeBuilder& RecipeBuilder::output(ItemId item, int64_t count) {
    recipe_.outputs.push_back(RecipeOutput{item, count, 1.0f});
    return *this;
}

RecipeBuilder& RecipeBuilder::chanced_output(ItemId item, int64_t count,
                                              float probability) {
    recipe_.outputs.push_back(RecipeOutput{item, count, probability});
    return *this;
}

RecipeBuilder& RecipeBuilder::duration_ticks(int64_t ticks) {
    recipe_.duration_ticks = ticks;
    return *this;
}

RecipeBuilder& RecipeBuilder::eu_per_tick(int64_t eu) {
    recipe_.eu_per_tick = eu;
    return *this;
}

RecipeBuilder& RecipeBuilder::tier(VoltageTier min_tier) {
    recipe_.min_tier = min_tier;
    return *this;
}

RecipeBuilder& RecipeBuilder::duration_seconds(float seconds) {
    recipe_.duration_ticks = static_cast<int64_t>(seconds * 20.0f);
    return *this;
}

Recipe RecipeBuilder::build() const {
    assert(recipe_.duration_ticks > 0);
    assert(recipe_.eu_per_tick >= 0);
    assert(!recipe_.inputs.empty());
    assert(!recipe_.outputs.empty());
    return recipe_;
}

// ============================================================
// RecipeMap
// ============================================================

RecipeMap::RecipeMap(const char* machine_type)
    : machine_type_(machine_type) {}

void RecipeMap::add(const Recipe& recipe) {
    assert(std::strcmp(recipe.machine_type, machine_type_.c_str()) == 0);
    recipes_.push_back(recipe);
}

void RecipeMap::add(Recipe&& recipe) {
    assert(std::strcmp(recipe.machine_type, machine_type_.c_str()) == 0);
    recipes_.push_back(std::move(recipe));
}

std::vector<const Recipe*> RecipeMap::find_matching(
        const std::vector<ItemStack>& items) const {
    std::vector<const Recipe*> matches;
    for (const auto& recipe : recipes_) {
        if (recipe.matches_inputs(items)) {
            matches.push_back(&recipe);
        }
    }
    return matches;
}

const Recipe* RecipeMap::find_first_matching(
        const std::vector<ItemStack>& items) const {
    for (const auto& recipe : recipes_) {
        if (recipe.matches_inputs(items)) {
            return &recipe;
        }
    }
    return nullptr;
}

const Recipe* RecipeMap::find_by_name(const char* name) const {
    for (const auto& recipe : recipes_) {
        if (std::strcmp(recipe.name, name) == 0) {
            return &recipe;
        }
    }
    return nullptr;
}

// ============================================================
// RecipeDatabase — global registry
// ============================================================

namespace {

// Internal: string → RecipeMap* mapping.
struct RecipeDb {
    std::unordered_map<std::string, RecipeMap> maps;

    static RecipeDb& instance() {
        static RecipeDb db;
        return db;
    }
};

} // anonymous namespace

RecipeMap* RecipeDatabase::find_or_create_map(const char* machine_type) {
    auto& maps = RecipeDb::instance().maps;
    std::string key(machine_type);

    auto it = maps.find(key);
    if (it != maps.end()) {
        return &it->second;
    }

    auto [it2, _] = maps.emplace(key, RecipeMap(machine_type));
    return &it2->second;
}

void RecipeDatabase::initialize() {
    RecipeDb::instance().maps.clear();
}

RecipeMap* RecipeDatabase::get_map(const char* machine_type) {
    return find_or_create_map(machine_type);
}

void RecipeDatabase::add_recipe(const Recipe& recipe) {
    RecipeMap* map = find_or_create_map(recipe.machine_type);
    map->add(recipe);
}

std::vector<const char*> RecipeDatabase::get_machine_types() {
    std::vector<const char*> types;
    for (const auto& [key, _] : RecipeDb::instance().maps) {
        types.push_back(key.c_str());
    }
    return types;
}

size_t RecipeDatabase::get_total_recipe_count() {
    size_t total = 0;
    for (const auto& [_, map] : RecipeDb::instance().maps) {
        total += map.recipe_count();
    }
    return total;
}

} // namespace science_and_theology::gt
