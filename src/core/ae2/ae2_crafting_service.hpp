#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common/resource_key.hpp"
#include "ae2_pattern.hpp"
#include "ae2_pattern_provider.hpp"
#include "ae2_me_network.hpp"
#include "ae2_crafting_tree.hpp"
#include "ae2_crafting_resolver.hpp"
#include "ae2_crafting_cpu.hpp"

namespace science_and_theology::gt {

// ============================================================
// CraftingSubmitResult — result of submitting a job
// ============================================================

struct CraftingSubmitResult {
    bool success = false;
    const char* error_message = "";
    CraftingPlan plan;
};

// ============================================================
// CraftingService — manages CPUs and coordinates autocrafting
// ============================================================
//
// Mirrors AE2's CraftingService + CraftingGridCache.

class CraftingService {
public:
    CraftingService() = default;

    // Initialize: register built-in patterns and resolvers.
    void initialize();

    // --- CPU management ---

    // Register a crafting CPU.
    void add_cpu(CraftingCPU* cpu);

    // Remove a crafting CPU.
    void remove_cpu(CraftingCPU* cpu);

    // Find the best available CPU for a job.
    CraftingCPU* find_best_cpu(int64_t required_bytes);

    // --- Job submission ---

    // Submit a crafting job.
    // what: item to craft, mount: how many.
    // Returns the result with the plan.
    CraftingSubmitResult submit_job(ItemId what, int64_t amount);

    // --- Tick ---

    // Tick all active CPUs.
    void tick();

    // --- Patterns ---

    // Access the pattern registry.
    PatternRegistry& patterns() { return pattern_registry_; }
    const PatternRegistry& patterns() const { return pattern_registry_; }

    // Register a callback for job status changes.
    using JobCallback = std::function<void(ItemId what, int64_t amount, bool completed)>;
    void set_job_callback(JobCallback cb) { job_callback_ = std::move(cb); }

    // --- Network integration ---

    // Set the ME Network for direct item queries.
    void set_me_network(MENetwork* network) { me_network_ = network; }
    MENetwork* me_network() const { return me_network_; }

    // Callback for checking available items in the network (fallback).
    // Returns how many of item_id are available.
    using NetworkCheckCallback = std::function<int64_t(ItemId)>;
    void set_network_check_callback(NetworkCheckCallback cb) {
        network_check_ = std::move(cb);
    }

    // Callback for extracting items from the network.
    // Returns how many were actually extracted.
    using NetworkExtractCallback = std::function<int64_t(ItemId, int64_t)>;
    void set_network_extract_callback(NetworkExtractCallback cb) {
        network_extract_ = std::move(cb);
    }

    // Callback for inserting items into the network.
    using NetworkInsertCallback = std::function<int64_t(ItemId, int64_t)>;
    void set_network_insert_callback(NetworkInsertCallback cb) {
        network_insert_ = std::move(cb);
    }

    // Query network for available items.
    int64_t network_check(ItemId item_id) const;

    // Extract from network.
    int64_t network_extract(ItemId item_id, int64_t amount);

    // Insert into network.
    int64_t network_insert(ItemId item_id, int64_t amount);

    // --- Pattern providers ---

    // Register a pattern provider (e.g. an ME Interface machine).
    void add_pattern_provider(PatternProvider* provider) {
        provider_host_.add_provider(provider);
    }
    void remove_pattern_provider(PatternProvider* provider) {
        provider_host_.remove_provider(provider);
    }

    // Sync all provider patterns into the registry.
    // Call after providers change.
    void sync_providers() { provider_host_.sync_to_registry(); }

    // --- Emitable items ---

    void set_emitable(ItemId item_id, bool emitable) {
        pattern_registry_.set_emitable(item_id, emitable);
    }

    // Access pattern provider host for advanced use.
    PatternProviderHost& provider_host() { return provider_host_; }

private:
    PatternRegistry pattern_registry_;
    PatternProviderHost provider_host_;
    MENetwork* me_network_ = nullptr;
    std::vector<CraftingCPU*> cpus_;

    JobCallback job_callback_;
    NetworkCheckCallback network_check_;
    NetworkExtractCallback network_extract_;
    NetworkInsertCallback network_insert_;

    // Cache for resolvers registration.
    bool resolvers_initialized_ = false;
};

} // namespace science_and_theology::gt
