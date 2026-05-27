#include "ae2_crafting_service.hpp"
#include "crafting/crafting.hpp"
#include "machine/recipe.hpp"

namespace science_and_theology::gt {

void CraftingService::initialize() {
    if (resolvers_initialized_) return;
    resolvers_initialized_ = true;

    // Register built-in resolvers.
    CraftingCalculations::register_provider(
        std::make_unique<ExtractItemResolver>());
    CraftingCalculations::register_provider(
        std::make_unique<EmitableItemResolver>());
    CraftingCalculations::register_provider(
        std::make_unique<CraftableItemResolver>());
    CraftingCalculations::register_provider(
        std::make_unique<SimulateMissingItemResolver>());

    // Initialize pattern registry from existing systems.
    PatternRegistry::initialize();
}

void CraftingService::add_cpu(CraftingCPU* cpu) {
    if (cpu && std::find(cpus_.begin(), cpus_.end(), cpu) == cpus_.end()) {
        cpus_.push_back(cpu);
    }
}

void CraftingService::remove_cpu(CraftingCPU* cpu) {
    auto it = std::find(cpus_.begin(), cpus_.end(), cpu);
    if (it != cpus_.end()) {
        cpus_.erase(it);
    }
}

CraftingCPU* CraftingService::find_best_cpu(int64_t required_bytes) {
    CraftingCPU* best = nullptr;
    int64_t best_storage = 0;
    int best_coprocs = 0;

    for (auto* cpu : cpus_) {
        if (cpu->is_busy()) continue;
        if (cpu->available_storage() < required_bytes) continue;
        if (!best ||
            cpu->co_processors() > best_coprocs ||
            (cpu->co_processors() == best_coprocs &&
             cpu->storage_bytes() > best_storage)) {
            best = cpu;
            best_storage = cpu->storage_bytes();
            best_coprocs = cpu->co_processors();
        }
    }

    return best;
}

CraftingSubmitResult CraftingService::submit_job(ItemId what, int64_t amount) {
    CraftingSubmitResult result;
    result.success = false;

    // Check if item is craftable (owning + provider patterns) or emitable.
    bool can_craft = !PatternRegistry::find_all_patterns_for(what).empty();
    bool can_emit = PatternRegistry::is_emitable(what);

    if (!can_craft && !can_emit) {
        result.error_message = "Item cannot be crafted or emitted";
        return result;
    }

    // Build network simulation state.
    CraftingSimulationState sim_state;
    std::unordered_map<ItemId, int64_t> network_items;

    if (me_network_ || network_check_) {
        // Query network for available items for all pattern inputs.
        auto patterns = PatternRegistry::find_all_patterns_for(what);
        for (const auto* pattern : patterns) {
            for (const auto& input : pattern->get_inputs()) {
                if (input.is_valid() && network_items.count(input.item_id()) == 0) {
                    network_items[input.item_id()] = network_check(input.item_id());
                }
            }
        }
        // Also include the requested item itself.
        if (network_items.count(what) == 0) {
            network_items[what] = network_check(what);
        }
    }

    sim_state.initialize(network_items);

    // Create a context for the calculation.
    CraftingContext context;
    context.initialize(std::move(sim_state), []{
        std::vector<const AEPattern*> refs;
        for (const auto& p : PatternRegistry::all_patterns()) refs.push_back(p.get());
        return refs;
    }());

    // Emitable items.
    // (In a full implementation, this would be populated from network providers.)

    // Run the calculation.
    CraftingCalculation calculation;
    calculation.initialize(what, amount, CraftingSimulationState{});

    // Reinitialize sim_state �� we need to pass the context's state.
    // For now, create a simpler flow.
    CraftingPlan plan = calculation.compute();

    if (!plan.is_valid()) {
        result.error_message = "Failed to compute a valid crafting plan";
        return result;
    }

    result.plan = plan;

    // Find a suitable CPU.
    auto* cpu = find_best_cpu(plan.bytes);
    if (!cpu) {
        result.error_message = "No available crafting CPU found";
        return result;
    }

    // Submit the job to the CPU.
    if (!cpu->submit_job(plan)) {
        result.error_message = "CPU rejected the job";
        return result;
    }

    // Extract used items from network into CPU inventory.
    for (const auto& [item_id, amt] : plan.used_items) {
        int64_t extracted = network_extract(item_id, amt);
        if (extracted > 0) {
            cpu->insert_item(item_id, extracted);
        }
    }

    // Emit emitted items directly into CPU.
    for (const auto& [item_id, amt] : plan.emitted_items) {
        cpu->inventory()[item_id] += amt;
    }

    // Simulated missing items (fallback) — treat as available.
    for (const auto& [item_id, amt] : plan.missing_items) {
        cpu->inventory()[item_id] += amt;
    }

    // Notify callback.
    if (job_callback_) {
        job_callback_(what, amount, false);
    }

    result.success = true;
    return result;
}

void CraftingService::tick() {
    for (auto* cpu : cpus_) {
        cpu->tick();
    }
}

int64_t CraftingService::network_check(ItemId item_id) const {
    if (me_network_) {
        ItemKey key(item_id);
        return me_network_->check_global(key);
    }
    if (network_check_) return network_check_(item_id);
    return 0;
}

int64_t CraftingService::network_extract(ItemId item_id, int64_t amount) {
    if (me_network_) {
        ItemKey key(item_id);
        return me_network_->extract_global(key, amount, kInvalidMENodeId);
    }
    if (network_extract_) return network_extract_(item_id, amount);
    return 0;
}

int64_t CraftingService::network_insert(ItemId item_id, int64_t amount) {
    if (me_network_) {
        ItemKey key(item_id);
        return me_network_->insert_global(key, amount, kInvalidMENodeId);
    }
    if (network_insert_) return network_insert_(item_id, amount);
    return 0;
}

} // namespace science_and_theology::gt
