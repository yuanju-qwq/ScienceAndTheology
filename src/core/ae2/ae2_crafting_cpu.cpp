#include "ae2_crafting_cpu.hpp"

#include <algorithm>
#include <cmath>

namespace science_and_theology::gt {

// ============================================================
// ExecutingCraftingJob
// ============================================================

ExecutingCraftingJob::ExecutingCraftingJob(const CraftingPlan& plan)
    : plan_(plan)
    , remaining_amount_(plan.final_output.amount) {
    // Copy pattern tasks.
    for (const auto& [pattern, times] : plan.pattern_times) {
        tasks_[pattern].remaining = times;
    }
    // Copy emitted items to waiting_for.
    for (const auto& [item_id, amount] : plan.emitted_items) {
        waiting_for_[item_id] += amount;
    }
}

// ============================================================
// CraftingCPU
// ============================================================

void CraftingCPU::configure(int64_t storage_bytes, int co_processors) {
    storage_bytes_ = storage_bytes;
    co_processors_ = co_processors;
}

bool CraftingCPU::submit_job(const CraftingPlan& plan) {
    if (is_busy()) return false;
    if (plan.bytes > available_storage()) return false;

    used_bytes_ = plan.bytes;
    active_job_ = std::make_unique<ExecutingCraftingJob>(plan);

    return true;
}

void CraftingCPU::tick() {
    if (!active_job_) return;

    // Calculate remaining operations for this tick.
    int avg_ops = (ops_history_[0] + ops_history_[1] + ops_history_[2]) / 3;
    int max_ops = co_processors_ + 1;
    int remaining_ops = std::max(1, max_ops - avg_ops);

    ops_this_tick_ = 0;

    auto& tasks = active_job_->tasks();

    // Iterate tasks and try to dispatch patterns that have available inputs.
    for (auto it = tasks.begin();
         it != tasks.end() && ops_this_tick_ < remaining_ops; ) {

        auto& [pattern, progress] = *it;

        if (progress.remaining <= 0) {
            ++it;
            continue;
        }

        // Check if all inputs are in CPU inventory.
        auto inputs = pattern->get_inputs();
        bool inputs_ready = true;
        for (const auto& input : inputs) {
            if (!input.is_valid()) continue;
            auto inv_it = inventory_.find(input.item_id());
            if (inv_it == inventory_.end() || inv_it->second < input.amount) {
                inputs_ready = false;
                break;
            }
        }

        if (!inputs_ready) {
            ++it;
            continue;
        }

        // Consume inputs from CPU inventory.
        for (const auto& input : inputs) {
            if (!input.is_valid()) continue;
            inventory_[input.item_id()] -= input.amount;
            if (inventory_[input.item_id()] <= 0) {
                inventory_.erase(input.item_id());
            }
        }

        // Dispatch to appropriate machine callback.
        bool dispatched = false;
        if (pattern->is_crafting_pattern()) {
            if (craft_executor_) {
                dispatched = craft_executor_(this, pattern, 1);
            }
        } else {
            if (process_executor_) {
                dispatched = process_executor_(this, pattern, 1);
            }
        }

        if (dispatched) {
            // Track expected outputs.
            auto outputs = pattern->get_outputs();
            for (const auto& output : outputs) {
                if (!output.is_valid()) continue;
                active_job_->waiting_for()[output.item_id()] += output.amount;
            }

            progress.remaining--;
            progress.dispatched++;
            ops_this_tick_++;
        } else {
            // Dispatch failed — put inputs back.
            for (const auto& input : inputs) {
                if (!input.is_valid()) continue;
                inventory_[input.item_id()] += input.amount;
            }
            ++it;
        }
    }

    // Roll ops history for throttling.
    ops_history_[2] = ops_history_[1];
    ops_history_[1] = ops_history_[0];
    ops_history_[0] = ops_this_tick_;

    // Try to emit items directly (no machine needed).
    auto& waiting = active_job_->waiting_for();
    for (auto wit = waiting.begin(); wit != waiting.end(); ) {
        if (PatternRegistry::is_emitable(wit->first)) {
            // Emitted items appear directly in inventory.
            inventory_[wit->first] += wit->second;
            wit = waiting.erase(wit);
        } else {
            ++wit;
        }
    }

    // Check if job is complete.
    if (active_job_->is_complete()) {
        store_items();
        active_job_.reset();
    }
}

int64_t CraftingCPU::insert_item(ItemId item_id, int64_t amount) {
    if (!active_job_) return 0;

    auto& waiting = active_job_->waiting_for();
    auto it = waiting.find(item_id);

    // Final output arrives — decrement remaining amount directly.
    if (item_id == active_job_->final_output().item_id()) {
        int64_t accepted = std::min(amount, active_job_->remaining_amount());
        active_job_->set_remaining_amount(
            active_job_->remaining_amount() - accepted);

        if (it != waiting.end()) {
            it->second -= accepted;
            if (it->second <= 0) waiting.erase(it);
        }

        // Decrement dispatched for patterns producing this.
        auto& tasks = active_job_->tasks();
        for (auto& [pattern, progress] : tasks) {
            if (progress.dispatched <= 0) continue;
            for (const auto& output : pattern->get_outputs()) {
                if (output.item_id() == item_id) {
                    progress.dispatched = std::max((int64_t)0, progress.dispatched - 1);
                    break;
                }
            }
        }

        return accepted;
    }

    // Intermediate item being waited for (machine output).
    if (it != waiting.end()) {
        int64_t accepted = std::min(amount, it->second);
        it->second -= accepted;
        if (it->second <= 0) waiting.erase(it);

        // Decrement dispatched counter.
        auto& tasks = active_job_->tasks();
        for (auto& [pattern, progress] : tasks) {
            if (progress.dispatched <= 0) continue;
            for (const auto& output : pattern->get_outputs()) {
                if (output.item_id() == item_id) {
                    progress.dispatched = std::max((int64_t)0, progress.dispatched - 1);
                    break;
                }
            }
        }

        // Store in inventory for downstream patterns.
        inventory_[item_id] += accepted;
        return accepted;
    }

    // Raw material — not in waiting_for, store directly in inventory.
    inventory_[item_id] += amount;
    return amount;
}

bool CraftingCPU::is_waiting_for(ItemId item_id) const {
    if (!active_job_) return false;
    const auto& waiting = active_job_->waiting_for();
    return waiting.find(item_id) != waiting.end();
}

bool CraftingCPU::is_pattern_dispatched(const AEPattern* pattern) const {
    if (!active_job_) return false;
    const auto& tasks = active_job_->tasks();
    auto it = tasks.find(pattern);
    if (it == tasks.end()) return false;
    return it->second.dispatched > 0;
}

void CraftingCPU::cancel_job() {
    if (active_job_) {
        store_items();
        active_job_.reset();
        used_bytes_ = 0;
    }
}

void CraftingCPU::store_items() {
    // Return remaining inventory items.
    // In the full implementation, this would push items back to the network.
    inventory_.clear();
}

} // namespace science_and_theology::gt
