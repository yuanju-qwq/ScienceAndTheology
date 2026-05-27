#include "ae2_crafting_resolver.hpp"
#include "ae2_pattern.hpp"
#include "ae2_crafting_tree.hpp"

#include <algorithm>
#include <cmath>

namespace science_and_theology::gt {

// ============================================================
// CraftingRequest
// ============================================================

CraftingRequest::CraftingRequest(ItemId what, int64_t amount,
                                 CraftingRequest* parent,
                                 bool allow_simulation)
    : what_(what)
    , original_amount_(amount)
    , remaining_to_process_(amount)
    , parent_(parent)
    , allow_simulation_(allow_simulation) {
    if (parent) {
        ancestors_ = parent->ancestors_;
        ancestors_.push_back(parent);
    }
}

void CraftingRequest::fulfill(CraftingTask* task, ItemId item, int64_t amount) {
    if (amount > remaining_to_process_) {
        amount = remaining_to_process_;
    }
    remaining_to_process_ -= amount;
    // Find existing entry or add new.
    bool found = false;
    for (auto& entry : used_resolvers_) {
        if (entry.task == task && entry.resolved_item == item) {
            entry.resolved_amount += amount;
            found = true;
            break;
        }
    }
    if (!found) {
        UsedResolverEntry entry;
        entry.task = task;
        entry.resolved_item = item;
        entry.resolved_amount = amount;
        used_resolvers_.push_back(entry);
    }
}

void CraftingRequest::partial_refund(int64_t amount) {
    int64_t remaining = amount;
    // Refund from last resolver first.
    for (auto it = used_resolvers_.rbegin(); it != used_resolvers_.rend() && remaining > 0; ++it) {
        int64_t refundable = std::min(remaining, it->resolved_amount);
        it->resolved_amount -= refundable;
        remaining -= refundable;
    }
    remaining_to_process_ += remaining;
}

void CraftingRequest::full_refund() {
    used_resolvers_.clear();
    remaining_to_process_ = original_amount_;
}

ItemId CraftingRequest::get_one_resolved_type() const {
    if (used_resolvers_.empty()) return kInvalidItemId;
    return used_resolvers_[0].resolved_item;
}

bool CraftingRequest::is_ancestor_of(const CraftingRequest& other) const {
    for (const auto* ancestor : other.ancestors_) {
        if (ancestor == this) return true;
    }
    return false;
}

// ============================================================
// CraftingContext
// ============================================================

void CraftingContext::initialize(CraftingSimulationState&& sim_state,
                                 const std::vector<const AEPattern*>& patterns) {
    sim_state_ = std::move(sim_state);
    pattern_index_.clear();
    emitable_items_.clear();

    for (const auto* pattern : patterns) {
        ResourceStack output = pattern->get_primary_output();
        if (output.is_valid()) {
            pattern_index_[output.item_id()].push_back(pattern);
        }
    }
}

void CraftingContext::add_request(CraftingRequest& request) {
    auto tasks = CraftingCalculations::try_resolve(request, *this);
    if (tasks.empty()) {
        return;
    }
    live_requests_.push_back(&request);

    // Sort by priority (highest first).
    std::sort(tasks.begin(), tasks.end(),
              [](const auto& a, const auto& b) {
                  return CraftingTask::compare_priority(a.get(), b.get());
              });

    // Add to task queue (highest priority last, so it gets popped first).
    for (auto& task : tasks) {
        task_queue_.push_back(task.get());
        resolved_tasks_.push_back(std::move(task));
    }
}

CraftingTask::State CraftingContext::do_work() {
    if (task_queue_.empty()) {
        return CraftingTask::State::SUCCESS;
    }

    // Pop highest priority task (last element).
    auto* task = task_queue_.back();
    task_queue_.pop_back();

    if (task->state() == CraftingTask::State::SUCCESS ||
        task->state() == CraftingTask::State::FAILURE) {
        return CraftingTask::State::NEEDS_MORE_WORK;
    }

    // Calculate.
    auto sub_requests = task->calculate_one_step(*this);

    // Add sub-requests.
    for (auto& sub_request : sub_requests) {
        add_request(sub_request);
    }

    return task_queue_.empty()
        ? CraftingTask::State::SUCCESS
        : CraftingTask::State::NEEDS_MORE_WORK;
}

std::vector<const AEPattern*> CraftingContext::get_patterns_for(ItemId item_id) const {
    auto it = pattern_index_.find(item_id);
    if (it != pattern_index_.end()) {
        return it->second;
    }
    return {};
}

// ============================================================
// CraftingCalculations
// ============================================================

struct CraftingCalculations::Impl {
    std::vector<std::unique_ptr<CraftingRequestResolver>> providers;
};

CraftingCalculations::Impl& CraftingCalculations::impl() {
    static Impl i;
    return i;
}

void CraftingCalculations::register_provider(
        std::unique_ptr<CraftingRequestResolver> provider) {
    impl().providers.push_back(std::move(provider));
}

std::vector<std::unique_ptr<CraftingTask>> CraftingCalculations::try_resolve(
        CraftingRequest& request, CraftingContext& context) {
    std::vector<std::unique_ptr<CraftingTask>> all_tasks;
    for (auto& provider : impl().providers) {
        auto tasks = provider->provide_resolvers(request, context);
        for (auto& task : tasks) {
            all_tasks.push_back(std::move(task));
        }
    }

    // Sort by priority descending.
    std::sort(all_tasks.begin(), all_tasks.end(),
              [](const auto& a, const auto& b) {
                  return CraftingTask::compare_priority(a.get(), b.get());
              });

    return all_tasks;
}

int64_t CraftingCalculations::adjust_byte_cost(int64_t raw_cost) {
    if (raw_cost <= 0) return 1;
    return raw_cost;  // Items: 1 byte per item.
}

// ============================================================
// ExtractItemResolver — ExtractItemTask
// ============================================================

namespace {

class ExtractItemTask : public CraftingTask {
public:
    explicit ExtractItemTask(CraftingRequest* request)
        : CraftingTask(request, kPriorityExtract) {}

    std::vector<CraftingRequest> calculate_one_step(
            CraftingContext& context) override {
        state_ = State::SUCCESS;

        if (request_->is_fulfilled()) return {};

        ItemId what = request_->what();
        int64_t needed = request_->remaining_to_process();
        int64_t available = context.sim_state().get_available(what);
        int64_t extracted = std::min(needed, available);

        if (extracted > 0) {
            context.sim_state().extract(what, extracted);
            removed_.push_back({what, extracted});
            request_->fulfill(this, what, extracted);
        }

        return {};
    }

    int64_t partial_refund(int64_t amount) override {
        int64_t remaining = amount;
        for (auto it = removed_.rbegin(); it != removed_.rend() && remaining > 0; ++it) {
            int64_t refund = std::min(remaining, it->second);
            it->second -= refund;
            remaining -= refund;
        }
        return amount - remaining;
    }

    void full_refund() override {
        removed_.clear();
        request_->full_refund();
    }

private:
    std::vector<std::pair<ItemId, int64_t>> removed_;
};

} // anonymous namespace

std::vector<std::unique_ptr<CraftingTask>> ExtractItemResolver::provide_resolvers(
        CraftingRequest& request, CraftingContext& context) {
    std::vector<std::unique_ptr<CraftingTask>> tasks;
    ItemId what = request.what();
    if (context.sim_state().get_available(what) > 0) {
        tasks.push_back(std::make_unique<ExtractItemTask>(&request));
    }
    return tasks;
}

// ============================================================
// EmitableItemResolver — EmitItemTask
// ============================================================

namespace {

class EmitItemTask : public CraftingTask {
public:
    explicit EmitItemTask(CraftingRequest* request)
        : CraftingTask(request, kPriorityEmit) {}

    std::vector<CraftingRequest> calculate_one_step(
            CraftingContext& context) override {
        state_ = State::SUCCESS;
        if (request_->is_fulfilled()) return {};

        ItemId what = request_->what();
        int64_t needed = request_->remaining_to_process();

        // Mark as emitted in simulation.
        context.sim_state().emit_items(what, needed);
        fulfilled_ = needed;
        request_->fulfill(this, what, needed);

        return {};
    }

    int64_t partial_refund(int64_t amount) override {
        int64_t refunded = std::min(amount, fulfilled_);
        fulfilled_ -= refunded;
        return refunded;
    }

    void full_refund() override {
        fulfilled_ = 0;
        request_->full_refund();
    }

private:
    int64_t fulfilled_ = 0;
};

} // anonymous namespace

std::vector<std::unique_ptr<CraftingTask>> EmitableItemResolver::provide_resolvers(
        CraftingRequest& request, CraftingContext& context) {
    std::vector<std::unique_ptr<CraftingTask>> tasks;
    if (context.can_emit(request.what())) {
        tasks.push_back(std::make_unique<EmitItemTask>(&request));
    }
    return tasks;
}

// ============================================================
// CraftableItemResolver — CraftFromPatternTask
// ============================================================

namespace {

class CraftFromPatternTask : public CraftingTask {
public:
    CraftFromPatternTask(CraftingRequest* request, const AEPattern* pattern,
                         int priority)
        : CraftingTask(request, priority), pattern_(pattern) {}

    std::vector<CraftingRequest> calculate_one_step(
            CraftingContext& context) override {
        if (!requested_inputs_) {
            // Phase 1: Create sub-requests for all inputs.
            requested_inputs_ = true;
            state_ = State::NEEDS_MORE_WORK;

            auto inputs = pattern_->get_inputs();
            ResourceStack output = pattern_->get_primary_output();
            int64_t per_craft = output.is_valid() ? output.amount : 1;
            if (per_craft <= 0) per_craft = 1;

            int64_t needed = request_->remaining_to_process();
            int64_t crafts_needed = (needed + per_craft - 1) / per_craft;

            for (const auto& input : inputs) {
                if (!input.is_valid()) continue;

                CraftingRequest sub_request(
                    input.item_id(),
                    input.amount * crafts_needed,
                    request_,
                    false);  // Don't allow simulation for sub-requests.

                // Track how much we requested.
                requested_inputs_list_.push_back({
                    input.item_id(),
                    input.amount,
                    static_cast<int>(requested_inputs_list_.size())
                });

                sub_requests_.push_back(std::move(sub_request));
            }

            // Return sub-requests to be processed.
            return sub_requests_;
        }

        // Phase 2: All sub-requests are resolved. Calculate how many we can craft.
        state_ = State::SUCCESS;

        ResourceStack output = pattern_->get_primary_output();
        int64_t per_craft = output.is_valid() ? output.amount : 1;
        if (per_craft <= 0) per_craft = 1;

        // Check how many crafts we can actually do based on available inputs.
        int64_t max_crafts = sub_requests_.empty() ? 1 : sub_requests_[0].remaining_to_process();

        // Calculate fulfilled amount.
        int64_t fulfilled = std::min(
            max_crafts * per_craft,
            request_->remaining_to_process());

        fulfilled_ = fulfilled;

        // Simulate crafting: consume inputs, produce outputs.
        auto inputs = pattern_->get_inputs();
        for (const auto& input : inputs) {
            if (!input.is_valid()) continue;
            context.sim_state().extract(input.item_id(),
                                        input.amount * max_crafts);
        }

        context.sim_state().insert(output.item_id(), fulfilled);
        context.sim_state().add_crafting(pattern_, max_crafts);

        request_->fulfill(this, output.item_id(), fulfilled);

        return {};
    }

    int64_t partial_refund(int64_t amount) override {
        int64_t refunded = std::min(amount, fulfilled_);
        fulfilled_ -= refunded;
        return refunded;
    }

    void full_refund() override {
        fulfilled_ = 0;
        request_->full_refund();
    }

private:
    const AEPattern* pattern_ = nullptr;
    bool requested_inputs_ = false;
    int64_t fulfilled_ = 0;
    std::vector<CraftingRequest> sub_requests_;
    struct InputInfo {
        ItemId item_id;
        int64_t amount;
        int index;
    };
    std::vector<InputInfo> requested_inputs_list_;
};

} // anonymous namespace

std::vector<std::unique_ptr<CraftingTask>> CraftableItemResolver::provide_resolvers(
        CraftingRequest& request, CraftingContext& context) {
    std::vector<std::unique_ptr<CraftingTask>> tasks;
    auto patterns = context.get_patterns_for(request.what());
    int priority = kPriorityCraftBase;
    for (const auto* pattern : patterns) {
        tasks.push_back(
            std::make_unique<CraftFromPatternTask>(&request, pattern, --priority));
    }
    return tasks;
}

// ============================================================
// SimulateMissingItemResolver — ConjureItemTask
// ============================================================

namespace {

class ConjureItemTask : public CraftingTask {
public:
    explicit ConjureItemTask(CraftingRequest* request)
        : CraftingTask(request, kPrioritySimulate) {
        simulated_ = true;
    }

    std::vector<CraftingRequest> calculate_one_step(
            CraftingContext& context) override {
        state_ = State::SUCCESS;
        if (request_->is_fulfilled()) return {};

        ItemId what = request_->what();
        int64_t needed = request_->remaining_to_process();
        fulfilled_ = needed;

        request_->fulfill(this, what, needed);
        return {};
    }

    int64_t partial_refund(int64_t amount) override {
        int64_t refunded = std::min(amount, fulfilled_);
        fulfilled_ -= refunded;
        return refunded;
    }

    void full_refund() override {
        fulfilled_ = 0;
        request_->full_refund();
    }

private:
    int64_t fulfilled_ = 0;
};

} // anonymous namespace

std::vector<std::unique_ptr<CraftingTask>> SimulateMissingItemResolver::provide_resolvers(
        CraftingRequest& request, CraftingContext& context) {
    std::vector<std::unique_ptr<CraftingTask>> tasks;
    // Only activate for simulation mode.
    if (context.was_simulated()) {
        tasks.push_back(std::make_unique<ConjureItemTask>(&request));
    }
    return tasks;
}

} // namespace science_and_theology::gt
