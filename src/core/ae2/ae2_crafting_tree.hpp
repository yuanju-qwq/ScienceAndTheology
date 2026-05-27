#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "common/resource_key.hpp"
#include "ae2_pattern.hpp"

namespace science_and_theology::gt {

// Forward declarations
class AEPattern;
class CraftingSimulationState;

// ============================================================
// CraftingPlan �� result of crafting calculation
// ============================================================
//
// Mirrors AE2's CraftingPlan record.
// Contains everything needed to execute a crafting job.

struct CraftingPlan {
    ResourceStack final_output;                            // what was requested, with planned amount
    int64_t bytes = 0;                                     // total CPU storage bytes
    bool simulation = false;                               // true if this is a simulated (fallback) plan
    bool multiple_paths = false;                           // true if multiple pattern paths exist

    // Items to extract from network storage (raw materials).
    std::unordered_map<ItemId, int64_t> used_items;

    // Items to emit (produce without crafting, e.g. fluids).
    std::unordered_map<ItemId, int64_t> emitted_items;

    // Items that are missing/unavailable in simulation mode.
    std::unordered_map<ItemId, int64_t> missing_items;

    // Patterns -> how many times to run each.
    std::unordered_map<const AEPattern*, int64_t> pattern_times;

    bool is_valid() const { return final_output.is_valid(); }
};

// ============================================================
// CraftingSimulationState �� copy-on-write inventory snapshot
// ============================================================
//
// Mirrors AE2's CraftingSimulationState.
// Manages a working copy of available items for simulation.

class CraftingSimulationState {
public:
    CraftingSimulationState() = default;

    // Initialize from a map of available items (e.g., from ItemPipeNetwork).
    void initialize(const std::unordered_map<ItemId, int64_t>& available);

    // --- Query ---

    // Check how many of an item are available.
    int64_t get_available(ItemId item_id) const;

    // Check if at least mount of item_id can be extracted.
    bool can_extract(ItemId item_id, int64_t amount) const;

    // --- Mutation ---

    // Extract items (subtract from working copy).
    // Returns the amount actually extracted (may be less than requested).
    int64_t extract(ItemId item_id, int64_t amount);

    // Insert items into the working copy.
    void insert(ItemId item_id, int64_t amount);

    // Mark items as emitted (produced without crafting).
    void emit_items(ItemId item_id, int64_t amount);

    // Mark items as missing (for simulation fallback).
    void mark_missing(ItemId item_id, int64_t amount);

    // Add byte cost for CPU storage calculation.
    void add_bytes(double b) { bytes_ += b; }

    // Record a pattern usage.
    void add_crafting(const AEPattern* pattern, int64_t times);

    // --- Diff / Merge ---

    // Apply changes from a child state back to this state.
    void apply_diff(const CraftingSimulationState& child);

    // --- Finalization ---

    // Build a CraftingPlan from the current state.
    CraftingPlan build_plan(ResourceStack final_output, bool simulation,
                            bool multiple_paths) const;

    // --- Access ---

    double get_bytes() const { return bytes_; }
    const std::unordered_map<ItemId, int64_t>& get_working() const { return modifiable_; }
    const std::unordered_map<ItemId, int64_t>& get_emitted() const { return emitted_; }
    const std::unordered_map<ItemId, int64_t>& get_missing() const { return missing_; }
    const std::unordered_map<const AEPattern*, int64_t>& get_crafts() const { return crafts_; }

private:
    // Unmodified snapshot (from network).
    std::unordered_map<ItemId, int64_t> unmodified_;

    // Working copy (tracks what we've taken/added).
    std::unordered_map<ItemId, int64_t> modifiable_;

    // Items that are emitted.
    std::unordered_map<ItemId, int64_t> emitted_;

    // Items that are missing.
    std::unordered_map<ItemId, int64_t> missing_;
    std::unordered_map<ItemId, int64_t> base_snapshot_;

    // Maximum extract recorded (for plan.used_items).
    std::unordered_map<ItemId, int64_t> required_extract_;

    // CPU byte cost.
    double bytes_ = 0.0;

    // Pattern usage count.
    std::unordered_map<const AEPattern*, int64_t> crafts_;
};

// ============================================================
// CraftingTreeNode �� one item in the crafting tree
// ============================================================
//
// Mirrors AE2's CraftingTreeNode.
// Represents a single item that needs to be obtained.
// Resolution order: extract �� emit �� craft (using patterns).

class CraftingTreeProcess;

class CraftingTreeNode {
public:
    CraftingTreeNode() = default;

    // Full constructor.
    CraftingTreeNode(ItemId what, int64_t amount,
                     CraftingTreeProcess* parent_process = nullptr,
                     int input_slot = -1);

    ItemId what() const { return what_; }
    int64_t amount() const { return amount_; }
    bool can_emit() const { return can_emit_; }
    void set_can_emit(bool v) { can_emit_ = v; }

    // Build child patterns lazily.
    void build_child_patterns();

    // Request this many items from the simulation state.
    // Throws CraftBranchFailure on failure.
    void request(CraftingSimulationState& state, int64_t requested_amount);

    // Check if a pattern would cause recursion (infinite loop).
    bool not_recursive(const AEPattern* pattern) const;

    // Total nodes in this subtree (for debugging).
    int64_t get_node_count() const;

    // Returns true if multiple pattern paths exist.
    bool has_multiple_paths() const;

    CraftingTreeProcess* parent_process();
    const CraftingTreeProcess* parent_process() const;
    int input_slot() const;

private:
    ItemId what_ = kInvalidItemId;
    int64_t amount_ = 0;
    bool can_emit_ = false;

    // Parent process (null for root).
    CraftingTreeProcess* parent_process_ = nullptr;

    // Which input slot of the parent this node corresponds to.
    int input_slot_ = -1;

    // Child processes (patterns that can make this item).
    // Lazily initialized.
    std::vector<CraftingTreeProcess> processes_;
    bool patterns_built_ = false;
};

// ============================================================
// CraftingTreeProcess �� one pattern used in the tree
// ============================================================
//
// Mirrors AE2's CraftingTreeProcess.
// Represents one pattern application: its inputs become child nodes.

class CraftingTreeProcess {
public:
    CraftingTreeProcess() = default;
    CraftingTreeProcess(const AEPattern* pattern, CraftingTreeNode* parent_node);

    const AEPattern* pattern() const { return pattern_; }
    bool is_possible() const { return possible_; }
    void set_possible(bool v) { possible_ = v; }

    // Request 	imes applications of this pattern from the simulation state.
    void request(CraftingSimulationState& state, int64_t times);

    int64_t get_node_count() const;
    bool has_multiple_paths() const;

    const std::vector<std::pair<CraftingTreeNode, int64_t>>& child_nodes() const;
    std::vector<std::pair<CraftingTreeNode, int64_t>>& mutable_nodes();
    CraftingTreeNode* parent_node();
    const CraftingTreeNode* parent_node() const;

private:
    const AEPattern* pattern_ = nullptr;
    CraftingTreeNode* parent_node_ = nullptr;
    bool possible_ = true;

    // Child nodes (one per pattern input), with multipliers.
    std::vector<std::pair<CraftingTreeNode, int64_t>> nodes_;
};

// ============================================================
// CraftingCalculation �� orchestrates the planning phase
// ============================================================
//
// Mirrors AE2's CraftingCalculation.
// Given a request, computes a CraftingPlan.

class CraftingCalculation {
public:
    CraftingCalculation() = default;

    // Initialize with a request.
    void initialize(ItemId what, int64_t amount,
                    CraftingSimulationState&& network_state);

    // Compute the crafting plan.
    // Tries: full amount �� CRAFT_LESS binary search �� simulated fallback.
    CraftingPlan compute();

    // Access the root tree node.
    const CraftingTreeNode& root_node() const { return root_; }

    // Access the simulation state.
    const CraftingSimulationState& simulation_state() const { return sim_state_; }

    // Returns missing items.
    const std::unordered_map<ItemId, int64_t>& get_missing() const { return missing_; }

    bool has_multiple_paths() const { return multi_paths_; }

private:
    // Run a single crafting attempt.
    // Returns null on failure (CraftBranchFailure).
    CraftingPlan run_attempt(int64_t amount, bool simulate);

    ItemId what_ = kInvalidItemId;
    int64_t requested_amount_ = 0;
    CraftingTreeNode root_;
    CraftingSimulationState sim_state_;
    bool multi_paths_ = false;

    // Missing items accumulator.
    std::unordered_map<ItemId, int64_t> missing_;
    std::unordered_map<ItemId, int64_t> base_snapshot_;
};

} // namespace science_and_theology::gt
