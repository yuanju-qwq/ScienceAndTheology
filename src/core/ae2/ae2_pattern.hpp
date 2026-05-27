#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/resource_key.hpp"

namespace science_and_theology::gt {

struct CraftingRecipe;
struct Recipe;

// ============================================================
// AEPattern — base class for all autocrafting patterns
// ============================================================
//
// Mirrors AE2's IPatternDetails design.
// Two concrete types:
//   AECraftingPattern   — wraps a manual workbench CraftingRecipe
//   AEProcessingPattern — wraps a machine Recipe

class AEPattern {
public:
    virtual ~AEPattern() = default;

    virtual const char* name() const = 0;

    // Condensed inputs (deduplicated, no nulls).
    virtual std::vector<ResourceStack> get_inputs() const = 0;

    // All outputs. First is the primary output used for matching.
    virtual std::vector<ResourceStack> get_outputs() const = 0;

    // Primary output (get_outputs()[0]).
    ResourceStack get_primary_output() const;

    // Returns true if this is a crafting table pattern (vs. processing).
    virtual bool is_crafting_pattern() const = 0;

    // Per-input: how many of this input are consumed per craft.
    virtual int64_t get_input_multiplier(int input_index) const = 0;

    // Total crafts that fit in one pattern application.
    virtual int64_t get_crafts_per_pattern() const = 0;

    // Clone this pattern (used by PatternProviderHost to sync into registry).
    virtual std::unique_ptr<AEPattern> clone() const = 0;
};

// ============================================================
// AECraftingPattern — wraps a manual workbench recipe
// ============================================================

class AECraftingPattern : public AEPattern {
public:
    explicit AECraftingPattern(const CraftingRecipe* recipe);

    const char* name() const override;
    std::vector<ResourceStack> get_inputs() const override;
    std::vector<ResourceStack> get_outputs() const override;
    bool is_crafting_pattern() const override { return true; }
    int64_t get_input_multiplier(int input_index) const override;
    int64_t get_crafts_per_pattern() const override { return 1; }

    std::unique_ptr<AEPattern> clone() const override;

    const CraftingRecipe* recipe() const { return recipe_; }

private:
    const CraftingRecipe* recipe_;
    std::vector<ResourceStack> condensed_inputs_;
    std::vector<ResourceStack> outputs_;
};

// ============================================================
// AEProcessingPattern — wraps a machine Recipe
// ============================================================

class AEProcessingPattern : public AEPattern {
public:
    explicit AEProcessingPattern(const Recipe* recipe);

    const char* name() const override;
    std::vector<ResourceStack> get_inputs() const override;
    std::vector<ResourceStack> get_outputs() const override;
    bool is_crafting_pattern() const override { return false; }
    int64_t get_input_multiplier(int input_index) const override;
    int64_t get_crafts_per_pattern() const override { return 1; }

    std::unique_ptr<AEPattern> clone() const override;

    const Recipe* recipe() const { return recipe_; }

private:
    const Recipe* recipe_;
    std::vector<ResourceStack> condensed_inputs_;
    std::vector<ResourceStack> outputs_;
};

// ============================================================
// SimplePattern — inline data pattern (no recipe wrapper)
// ============================================================
//
// Used by PatternProvider machines (e.g. ME Interface) to provide
// patterns directly via inputs/outputs without wrapping an existing recipe.

class SimplePattern : public AEPattern {
public:
    SimplePattern(const char* name,
                  std::vector<ResourceStack> inputs,
                  std::vector<ResourceStack> outputs,
                  bool is_crafting = false);

    const char* name() const override;
    std::vector<ResourceStack> get_inputs() const override;
    std::vector<ResourceStack> get_outputs() const override;
    bool is_crafting_pattern() const override { return is_crafting_; }
    int64_t get_input_multiplier(int input_index) const override;
    int64_t get_crafts_per_pattern() const override { return 1; }
    std::unique_ptr<AEPattern> clone() const override;

private:
    std::string name_;
    std::vector<ResourceStack> inputs_;
    std::vector<ResourceStack> outputs_;
    bool is_crafting_ = false;
};

// ============================================================
// Pattern registry — maps craftable items to their patterns
// ============================================================

class PatternRegistry {
public:
    // Register all patterns from the existing CraftingManager and RecipeDatabase.
    static void initialize();

    // Add a single pattern.
    static void add_pattern(std::unique_ptr<AEPattern> pattern);

    // Find patterns that can produce the given item.
    static std::vector<const AEPattern*> find_patterns_for(ItemId item_id);

    // Find patterns that can produce the given item (fuzzy match by ResourceKey).
    static std::vector<const AEPattern*> find_patterns_for_key(const ResourceKey& key);

    // Returns all registered patterns.
    static const std::vector<std::unique_ptr<AEPattern>>& all_patterns();

    // Returns true if an item can be crafted from patterns.
    static bool is_craftable(ItemId item_id);

    // Returns true if an item can be emitted (produced without crafting).
    static bool is_emitable(ItemId item_id);

    // Register an item as emitable.
    static void set_emitable(ItemId item_id, bool emitable);

    // --- Pattern factory ---

    // Create a crafting pattern from a manual workbench recipe.
    static std::unique_ptr<AEPattern> create_crafting_pattern(const CraftingRecipe* recipe);

    // Create a processing pattern from a machine recipe.
    static std::unique_ptr<AEPattern> create_processing_pattern(const Recipe* recipe);

    // --- Provider (non-owning) patterns ---

    // Add a pattern owned by an external provider.
    // Does NOT take ownership; provider must keep the pattern alive.
    static void add_provider_pattern(const AEPattern* pattern, uint64_t provider_id);

    // Remove all patterns for a given provider_id.
    static void remove_provider_patterns(uint64_t provider_id);

    // Check both owning and provider indices.
    static std::vector<const AEPattern*> find_all_patterns_for(ItemId item_id);

private:
    struct Impl;
    static Impl& impl();
};

} // namespace science_and_theology::gt
