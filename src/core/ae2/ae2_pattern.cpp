#include "ae2_pattern.hpp"
#include "crafting/crafting.hpp"
#include "machine/recipe.hpp"
#include "material/material_item.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace science_and_theology::gt {

// ============================================================
// AEPattern
// ============================================================

ResourceStack AEPattern::get_primary_output() const {
    auto outputs = get_outputs();
    return outputs.empty() ? ResourceStack{} : outputs[0];
}

// ============================================================
// AECraftingPattern
// ============================================================

// Condense stacks: merge duplicate items, preserve order.
static std::vector<ResourceStack> condense_stacks(
        const std::vector<ResourceStack>& sparse) {
    std::unordered_map<ItemId, int64_t> merged;
    std::vector<ItemId> order;
    for (const auto& s : sparse) {
        if (!s.is_valid()) continue;
        ItemId id = s.item_id();
        if (merged.count(id) == 0) {
            order.push_back(id);
        }
        merged[id] += s.amount;
    }
    std::vector<ResourceStack> result;
    for (ItemId id : order) {
        result.push_back(ResourceStack::item(id, merged[id]));
    }
    return result;
}

AECraftingPattern::AECraftingPattern(const CraftingRecipe* recipe)
    : recipe_(recipe) {
    for (const auto& input : recipe->inputs) {
        if (input.is_valid()) {
            condensed_inputs_.push_back(input);
        }
    }
    condensed_inputs_ = condense_stacks(condensed_inputs_);

    if (recipe->output.is_valid()) {
        outputs_.push_back(recipe->output);
    }
}

const char* AECraftingPattern::name() const {
    return recipe_->name;
}

std::vector<ResourceStack> AECraftingPattern::get_inputs() const {
    return condensed_inputs_;
}

std::vector<ResourceStack> AECraftingPattern::get_outputs() const {
    return outputs_;
}

int64_t AECraftingPattern::get_input_multiplier(int input_index) const {
    if (input_index < 0 || input_index >= (int)condensed_inputs_.size()) return 0;
    return condensed_inputs_[input_index].amount;
}

std::unique_ptr<AEPattern> AECraftingPattern::clone() const {
    return std::make_unique<AECraftingPattern>(recipe_);
}

// ============================================================
// AEProcessingPattern
// ============================================================

AEProcessingPattern::AEProcessingPattern(const Recipe* recipe)
    : recipe_(recipe) {
    for (const auto& input : recipe->inputs) {
        if (input.is_valid()) {
            condensed_inputs_.push_back(input);
        }
    }
    condensed_inputs_ = condense_stacks(condensed_inputs_);

    for (const auto& output : recipe->outputs) {
        if (output.is_valid() && output.stack.is_valid()) {
            outputs_.push_back(output.stack);
        }
    }
}

const char* AEProcessingPattern::name() const {
    return recipe_->name;
}

std::vector<ResourceStack> AEProcessingPattern::get_inputs() const {
    return condensed_inputs_;
}

std::vector<ResourceStack> AEProcessingPattern::get_outputs() const {
    return outputs_;
}

int64_t AEProcessingPattern::get_input_multiplier(int input_index) const {
    if (input_index < 0 || input_index >= (int)condensed_inputs_.size()) return 0;
    return condensed_inputs_[input_index].amount;
}

std::unique_ptr<AEPattern> AEProcessingPattern::clone() const {
    return std::make_unique<AEProcessingPattern>(recipe_);
}

// ============================================================
// SimplePattern
// ============================================================

SimplePattern::SimplePattern(const char* name,
                             std::vector<ResourceStack> inputs,
                             std::vector<ResourceStack> outputs,
                             bool is_crafting)
    : name_(name)
    , inputs_(std::move(inputs))
    , outputs_(std::move(outputs))
    , is_crafting_(is_crafting) {}

const char* SimplePattern::name() const { return name_.c_str(); }
std::vector<ResourceStack> SimplePattern::get_inputs() const { return inputs_; }
std::vector<ResourceStack> SimplePattern::get_outputs() const { return outputs_; }

int64_t SimplePattern::get_input_multiplier(int input_index) const {
    if (input_index < 0 || input_index >= (int)inputs_.size()) return 0;
    return inputs_[input_index].amount;
}

std::unique_ptr<AEPattern> SimplePattern::clone() const {
    return std::make_unique<SimplePattern>(name_.c_str(), inputs_, outputs_, is_crafting_);
}

// ============================================================
// PatternRegistry
// ============================================================

struct PatternRegistry::Impl {
    std::vector<std::unique_ptr<AEPattern>> patterns;
    std::unordered_map<ItemId, std::vector<const AEPattern*>> craftable_index;
    std::unordered_set<ItemId> emitable_items;

    // Non-owning provider patterns.
    std::unordered_map<ItemId, std::vector<const AEPattern*>> provider_index;
    std::unordered_multimap<uint64_t, const AEPattern*> provider_owner_index;
};

PatternRegistry::Impl& PatternRegistry::impl() {
    static Impl i;
    return i;
}

void PatternRegistry::initialize() {
    auto& i = impl();
    i.patterns.clear();
    i.craftable_index.clear();
    i.emitable_items.clear();
}

void PatternRegistry::add_pattern(std::unique_ptr<AEPattern> pattern) {
    auto& i = impl();
    ResourceStack output = pattern->get_primary_output();
    if (output.is_valid()) {
        ItemId id = output.item_id();
        i.patterns.push_back(std::move(pattern));
        const AEPattern* ptr = i.patterns.back().get();
        i.craftable_index[id].push_back(ptr);
    }
}

std::vector<const AEPattern*> PatternRegistry::find_patterns_for(ItemId item_id) {
    auto& i = impl();
    auto it = i.craftable_index.find(item_id);
    if (it != i.craftable_index.end()) {
        return it->second;
    }
    return {};
}

std::vector<const AEPattern*> PatternRegistry::find_patterns_for_key(
        const ResourceKey& key) {
    if (key.is_item()) {
        return find_patterns_for(key.as_item()->item_id());
    }
    return {};
}

const std::vector<std::unique_ptr<AEPattern>>& PatternRegistry::all_patterns() {
    return impl().patterns;
}

bool PatternRegistry::is_craftable(ItemId item_id) {
    auto& i = impl();
    return i.craftable_index.count(item_id) > 0;
}

bool PatternRegistry::is_emitable(ItemId item_id) {
    auto& i = impl();
    return i.emitable_items.count(item_id) > 0;
}

void PatternRegistry::set_emitable(ItemId item_id, bool emitable) {
    auto& i = impl();
    if (emitable) {
        i.emitable_items.insert(item_id);
    } else {
        i.emitable_items.erase(item_id);
    }
}

void PatternRegistry::add_provider_pattern(const AEPattern* pattern, uint64_t provider_id) {
    auto& i = impl();
    ResourceStack output = pattern->get_primary_output();
    if (output.is_valid()) {
        ItemId id = output.item_id();
        i.provider_index[id].push_back(pattern);
        i.provider_owner_index.insert({provider_id, pattern});
    }
}

void PatternRegistry::remove_provider_patterns(uint64_t provider_id) {
    auto& i = impl();
    auto [begin, end] = i.provider_owner_index.equal_range(provider_id);
    std::unordered_set<const AEPattern*> to_remove;
    for (auto it = begin; it != end; ++it) {
        to_remove.insert(it->second);
    }
    i.provider_owner_index.erase(provider_id);

    // Remove from provider_index.
    for (auto* pat : to_remove) {
        ResourceStack output = pat->get_primary_output();
        if (output.is_valid()) {
            ItemId id = output.item_id();
            auto& vec = i.provider_index[id];
            vec.erase(std::remove(vec.begin(), vec.end(), pat), vec.end());
            if (vec.empty()) {
                i.provider_index.erase(id);
            }
        }
    }
}

std::unique_ptr<AEPattern> PatternRegistry::create_crafting_pattern(const CraftingRecipe* recipe) {
    return std::make_unique<AECraftingPattern>(recipe);
}

std::unique_ptr<AEPattern> PatternRegistry::create_processing_pattern(const Recipe* recipe) {
    return std::make_unique<AEProcessingPattern>(recipe);
}

std::vector<const AEPattern*> PatternRegistry::find_all_patterns_for(ItemId item_id) {
    auto& i = impl();
    std::vector<const AEPattern*> result;

    // Check owning patterns.
    auto it = i.craftable_index.find(item_id);
    if (it != i.craftable_index.end()) {
        result = it->second;
    }

    // Check provider patterns.
    auto pit = i.provider_index.find(item_id);
    if (pit != i.provider_index.end()) {
        result.insert(result.end(), pit->second.begin(), pit->second.end());
    }

    return result;
}

} // namespace science_and_theology::gt
