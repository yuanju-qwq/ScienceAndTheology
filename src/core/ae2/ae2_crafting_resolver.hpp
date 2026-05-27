#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/resource_key.hpp"
#include "ae2_pattern.hpp"
#include "ae2_crafting_tree.hpp"

namespace science_and_theology::gt {

// Forward declarations
class CraftingContext;

// ============================================================
// CraftingRequest ！ a request for a specific item+amount
// ============================================================
//
// Mirrors AE2-Unofficial v2's CraftingRequest.
// Tracks how the request was fulfilled and supports refund.

class CraftingTask;

struct UsedResolverEntry {
    CraftingTask* task = nullptr;
    ItemId resolved_item = kInvalidItemId;
    int64_t resolved_amount = 0;
};

class CraftingRequest {
public:
    CraftingRequest() = default;

    // Full constructor.
    CraftingRequest(ItemId what, int64_t amount,
                    CraftingRequest* parent = nullptr,
                    bool allow_simulation = true);

    ItemId what() const { return what_; }
    int64_t original_amount() const { return original_amount_; }
    int64_t remaining_to_process() const { return remaining_to_process_; }
    bool is_fulfilled() const { return remaining_to_process_ <= 0; }

    // Record that this many items were fulfilled by a task.
    void fulfill(CraftingTask* task, ItemId item, int64_t amount);

    // Refund a partial amount (propagates to tasks).
    void partial_refund(int64_t amount);

    // Refund everything.
    void full_refund();

    // Returns the single resolved item type (for display).
    ItemId get_one_resolved_type() const;

    const std::vector<UsedResolverEntry>& used_resolvers() const {
        return used_resolvers_;
    }

    CraftingRequest* parent() const { return parent_; }

    // Check if this request is in the ancestor chain of another.
    bool is_ancestor_of(const CraftingRequest& other) const;

private:
    ItemId what_ = kInvalidItemId;
    int64_t original_amount_ = 0;
    int64_t remaining_to_process_ = 0;
    CraftingRequest* parent_ = nullptr;
    bool allow_simulation_ = true;

    std::vector<UsedResolverEntry> used_resolvers_;

    // Ancestor chain (for cycle prevention).
    std::vector<CraftingRequest*> ancestors_;
};

// ============================================================
// CraftingTask ！ one resolution step
// ============================================================
//
// Mirrors AE2-Unofficial v2's CraftingTask.
// Abstract base for: extract, emit, craft, simulate.

class CraftingTask {
public:
    enum class State : uint8_t {
        NEEDS_MORE_WORK,
        SUCCESS,
        FAILURE,
    };

    explicit CraftingTask(CraftingRequest* request, int priority)
        : request_(request), priority_(priority) {}
    virtual ~CraftingTask() = default;

    CraftingRequest* request() const { return request_; }
    int priority() const { return priority_; }
    State state() const { return state_; }
    bool is_simulated() const { return simulated_; }

    // Execute one step of calculation.
    // Returns the next batch of sub-requests needed.
    virtual std::vector<CraftingRequest> calculate_one_step(
            CraftingContext& context) = 0;

    // Partial refund: return mount items back.
    virtual int64_t partial_refund(int64_t amount) = 0;

    // Full refund: return all items.
    virtual void full_refund() = 0;

    // Priority comparator (highest first).
    static bool compare_priority(const CraftingTask* a, const CraftingTask* b) {
        return a->priority_ > b->priority_;
    }

protected:
    CraftingRequest* request_ = nullptr;
    int priority_ = 0;
    State state_ = State::NEEDS_MORE_WORK;
    bool simulated_ = false;
};

// Priority constants (matching AE2-Unofficial v2).
inline constexpr int kPriorityExtract = 2000000000 - 100;
inline constexpr int kPriorityEmit = kPriorityExtract - 200;
inline constexpr int kPriorityCraftBase = 0;
inline constexpr int kPrioritySimulateCraft = -2000000000 + 200;
inline constexpr int kPrioritySimulate = -2000000000 + 100;

// ============================================================
// CraftingContext ！ shared state for crafting resolution
// ============================================================
//
// Mirrors AE2-Unofficial v2's CraftingContext.

class CraftingContext {
public:
    CraftingContext() = default;

    // Initialize with the simulation state (network snapshot + patterns).
    void initialize(CraftingSimulationState&& sim_state,
                    const std::vector<const AEPattern*>& patterns);

    CraftingSimulationState& sim_state() { return sim_state_; }
    const CraftingSimulationState& sim_state() const { return sim_state_; }

    // Add a request ！ creates resolvers and starts processing.
    void add_request(CraftingRequest& request);

    // Main work loop ！ processes one task step.
    CraftingTask::State do_work();

    // Get patterns that can produce a given item.
    std::vector<const AEPattern*> get_patterns_for(ItemId item_id) const;

    // Access completed tasks.
    const std::vector<std::unique_ptr<CraftingTask>>& resolved_tasks() const {
        return resolved_tasks_;
    }

    // Check if simulation was used.
    bool was_simulated() const { return was_simulated_; }

    // Get/check emitable items.
    bool can_emit(ItemId item_id) const {
        return emitable_items_.count(item_id) > 0;
    }

private:
    CraftingSimulationState sim_state_;

    // Pattern index: item_id ★ list of patterns that produce it.
    std::unordered_map<ItemId, std::vector<const AEPattern*>> pattern_index_;

    // Items that can be emitted.
    std::unordered_set<ItemId> emitable_items_;

    // Active requests being processed.
    std::vector<CraftingRequest*> live_requests_;

    // Completed tasks (topologically sorted).
    std::vector<std::unique_ptr<CraftingTask>> resolved_tasks_;

    // Queue of tasks awaiting processing.
    std::vector<CraftingTask*> task_queue_;

    bool was_simulated_ = false;
};

// ============================================================
// CraftingRequestResolver ！ resolver provider interface
// ============================================================
//
// Mirrors AE2-Unofficial v2's CraftingRequestResolver.

class CraftingRequestResolver {
public:
    virtual ~CraftingRequestResolver() = default;

    // Given a request and context, return possible tasks (highest priority first).
    virtual std::vector<std::unique_ptr<CraftingTask>> provide_resolvers(
            CraftingRequest& request, CraftingContext& context) = 0;
};

// ============================================================
// CraftingCalculations ！ resolver registry
// ============================================================

class CraftingCalculations {
public:
    // Register a resolver provider.
    static void register_provider(std::unique_ptr<CraftingRequestResolver> provider);

    // Get all resolvers for a request, sorted by priority.
    static std::vector<std::unique_ptr<CraftingTask>> try_resolve(
            CraftingRequest& request, CraftingContext& context);

    // Adjust byte cost per unit type.
    static int64_t adjust_byte_cost(int64_t raw_cost);

private:
    struct Impl;
    static Impl& impl();
};

// ============================================================
// Concrete resolvers
// ============================================================

// ExtractItemResolver ！ extract from network inventory (highest priority).
class ExtractItemResolver : public CraftingRequestResolver {
public:
    std::vector<std::unique_ptr<CraftingTask>> provide_resolvers(
            CraftingRequest& request, CraftingContext& context) override;
};

// EmitableItemResolver ！ mark items for emission.
class EmitableItemResolver : public CraftingRequestResolver {
public:
    std::vector<std::unique_ptr<CraftingTask>> provide_resolvers(
            CraftingRequest& request, CraftingContext& context) override;
};

// CraftableItemResolver ！ craft using patterns (recursive).
class CraftableItemResolver : public CraftingRequestResolver {
public:
    std::vector<std::unique_ptr<CraftingTask>> provide_resolvers(
            CraftingRequest& request, CraftingContext& context) override;
};

// SimulateMissingItemResolver ！ simulate missing items (fallback).
class SimulateMissingItemResolver : public CraftingRequestResolver {
public:
    std::vector<std::unique_ptr<CraftingTask>> provide_resolvers(
            CraftingRequest& request, CraftingContext& context) override;
};

} // namespace science_and_theology::gt
