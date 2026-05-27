#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/resource_key.hpp"
#include "ae2_pattern.hpp"
#include "ae2_crafting_tree.hpp"

namespace science_and_theology::gt {

class CraftingCPUCluster;

// ============================================================
// ExecutingCraftingJob — a job running on a CPU
// ============================================================
//
// Mirrors AE2's ExecutingCraftingJob.

struct TaskProgress {
    int64_t remaining = 0;
    int64_t dispatched = 0;
};

class ExecutingCraftingJob {
public:
    ExecutingCraftingJob() = default;

    // Construct from a plan.
    explicit ExecutingCraftingJob(const CraftingPlan& plan);

    // The plan this job is executing.
    const CraftingPlan& plan() const { return plan_; }

    // Remaining tasks (pattern → count).
    std::unordered_map<const AEPattern*, TaskProgress>& tasks() { return tasks_; }
    const std::unordered_map<const AEPattern*, TaskProgress>& tasks() const {
        return tasks_;
    }

    // Items waiting to arrive from providers/machines.
    std::unordered_map<ItemId, int64_t>& waiting_for() { return waiting_for_; }
    const std::unordered_map<ItemId, int64_t>& waiting_for() const {
        return waiting_for_;
    }

    // Remaining amount of final output still needed.
    int64_t remaining_amount() const { return remaining_amount_; }
    void set_remaining_amount(int64_t v) { remaining_amount_ = v; }

    const ResourceStack& final_output() const { return plan_.final_output; }

    // True when no tasks remain, nothing is dispatched,
    // nothing is waiting, and the target amount is met.
    bool is_complete() const {
        if (remaining_amount_ > 0) return false;
        for (const auto& [_, tp] : tasks_) {
            if (tp.remaining > 0 || tp.dispatched > 0) return false;
        }
        return waiting_for_.empty();
    }

private:
    CraftingPlan plan_;
    std::unordered_map<const AEPattern*, TaskProgress> tasks_;
    std::unordered_map<ItemId, int64_t> waiting_for_;
    int64_t remaining_amount_ = 0;
};

// ============================================================
// CraftingCPU — dispatches patterns to machines, tracks results
// ============================================================
//
// Mirrors AE2's CraftingCpuLogic + CraftingCPUCluster.
// The CPU does NOT perform crafting itself. It dispatches:
//   - Crafting patterns → Molecular Assembler (craft_executor_)
//   - Processing patterns → external machines (process_executor_)

class CraftingCPU {
public:
    // Callback: dispatch a crafting pattern to a Molecular Assembler.
    // Parameters: this CPU, pattern, how many times to craft.
    // Returns: true if the craft was accepted by an assembler.
    // The assembler must call insert_item() when outputs are ready.
    using CraftExecutor = std::function<bool(
        CraftingCPU* cpu, const AEPattern* pattern, int64_t count)>;

    // Callback: dispatch a processing pattern to an external machine.
    // Parameters: this CPU, pattern, how many times to process.
    // Returns: true if the processing was accepted by a machine.
    // The machine must call insert_item() when outputs are ready.
    using ProcessExecutor = std::function<bool(
        CraftingCPU* cpu, const AEPattern* pattern, int64_t count)>;

    CraftingCPU() = default;
    ~CraftingCPU() = default;

    // Initialize with storage size and co-processor count.
    void configure(int64_t storage_bytes, int co_processors);

    int64_t storage_bytes() const { return storage_bytes_; }
    int co_processors() const { return co_processors_; }
    bool is_busy() const { return active_job_ != nullptr; }

    // Set executors. Must be set before the CPU can process patterns.
    void set_craft_executor(CraftExecutor executor) {
        craft_executor_ = std::move(executor);
    }
    void set_process_executor(ProcessExecutor executor) {
        process_executor_ = std::move(executor);
    }

    // Try to submit a job. Returns true if accepted.
    bool submit_job(const CraftingPlan& plan);

    // Tick the CPU — dispatch pending patterns to machines.
    void tick();

    // Insert items back (from machines / network).
    // If item_id matches the final output, decrements remaining_amount.
    // Otherwise stores in CPU inventory for later use.
    int64_t insert_item(ItemId item_id, int64_t amount);

    // Get stored items in CPU inventory.
    std::unordered_map<ItemId, int64_t>& inventory() { return inventory_; }
    const std::unordered_map<ItemId, int64_t>& inventory() const {
        return inventory_;
    }

    // Get available storage bytes.
    int64_t available_storage() const { return storage_bytes_ - used_bytes_; }

    // Get active job (nullptr if idle).
    ExecutingCraftingJob* active_job() { return active_job_.get(); }
    const ExecutingCraftingJob* active_job() const { return active_job_.get(); }

    // Check if a specific item is being waited for.
    bool is_waiting_for(ItemId item_id) const;

    // Check if any machine is currently working on the given pattern.
    bool is_pattern_dispatched(const AEPattern* pattern) const;

    // Cancel the current job.
    void cancel_job();

private:
    int64_t storage_bytes_ = 0;
    int64_t used_bytes_ = 0;
    int co_processors_ = 0;

    // CPU's internal storage (items extracted from network).
    std::unordered_map<ItemId, int64_t> inventory_;

    // Active job.
    std::unique_ptr<ExecutingCraftingJob> active_job_;

    // Pattern execution dispatch callbacks.
    CraftExecutor craft_executor_;
    ProcessExecutor process_executor_;

    // Co-processor throttling.
    int ops_this_tick_ = 0;
    int ops_history_[3] = {};

    void store_items();
    void try_dispatch_pattern(const AEPattern* pattern, TaskProgress& progress);
};

} // namespace science_and_theology::gt
