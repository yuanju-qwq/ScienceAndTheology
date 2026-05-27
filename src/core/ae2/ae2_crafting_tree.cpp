#include "ae2_crafting_tree.hpp"
#include "ae2_pattern.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace science_and_theology::gt {

// ============================================================
// CraftingBranchFailure �� exception for branch failure
// ============================================================

class CraftBranchFailure : public std::runtime_error {
public:
    explicit CraftBranchFailure(ItemId what, int64_t how_many)
        : std::runtime_error("Craft branch failed"), what_(what), how_many_(how_many) {}
    ItemId failed_item() const { return what_; }
    int64_t how_many() const { return how_many_; }
private:
    ItemId what_;
    int64_t how_many_;
};

// ============================================================
// CraftingSimulationState
// ============================================================

void CraftingSimulationState::initialize(
        const std::unordered_map<ItemId, int64_t>& available) {
    unmodified_ = available;
    modifiable_ = available;
    emitted_.clear();
    missing_.clear();
    required_extract_.clear();
    bytes_ = 0.0;
    crafts_.clear();
}

int64_t CraftingSimulationState::get_available(ItemId item_id) const {
    auto it = modifiable_.find(item_id);
    return it != modifiable_.end() ? it->second : 0;
}

bool CraftingSimulationState::can_extract(ItemId item_id, int64_t amount) const {
    return get_available(item_id) >= amount;
}

int64_t CraftingSimulationState::extract(ItemId item_id, int64_t amount) {
    int64_t available = get_available(item_id);
    int64_t taken = std::min(available, amount);
    if (taken > 0) {
        modifiable_[item_id] -= taken;
        int64_t total_taken = (unmodified_.count(item_id) ? unmodified_.at(item_id) : 0)
                            - (modifiable_.count(item_id) ? modifiable_.at(item_id) : 0);
        if (total_taken > required_extract_[item_id]) {
            required_extract_[item_id] = total_taken;
        }
    }
    return taken;
}

void CraftingSimulationState::insert(ItemId item_id, int64_t amount) {
    if (amount > 0) {
        modifiable_[item_id] += amount;
    }
}

void CraftingSimulationState::emit_items(ItemId item_id, int64_t amount) {
    if (amount > 0) {
        emitted_[item_id] += amount;
        insert(item_id, amount);
    }
}

void CraftingSimulationState::mark_missing(ItemId item_id, int64_t amount) {
    if (amount > 0) {
        missing_[item_id] += amount;
    }
}

void CraftingSimulationState::add_crafting(const AEPattern* pattern, int64_t times) {
    crafts_[pattern] += times;
}

void CraftingSimulationState::apply_diff(const CraftingSimulationState& child) {
    for (const auto& [id, amount] : child.required_extract_) {
        if (amount > required_extract_[id]) {
            required_extract_[id] = amount;
        }
    }
    // Merge net changes from child's modifiable back to parent's modifiable.
    for (const auto& [id, child_avail] : child.modifiable_) {
        int64_t child_unmodified = child.unmodified_.count(id) ? child.unmodified_.at(id) : 0;
        int64_t child_diff = child_avail - child_unmodified;

        int64_t parent_avail = get_available(id);
        int64_t parent_unmodified = unmodified_.count(id) ? unmodified_.at(id) : 0;
        int64_t parent_diff = parent_avail - parent_unmodified;

        int64_t net = child_diff - parent_diff;
        if (net > 0) insert(id, net);
        else if (net < 0) extract(id, -net);
    }
    for (const auto& [id, amount] : child.emitted_) {
        emitted_[id] += amount;
    }
    for (const auto& [id, amount] : child.missing_) {
        missing_[id] += amount;
    }
    bytes_ += child.bytes_;
    for (const auto& [pattern, times] : child.crafts_) {
        crafts_[pattern] += times;
    }
}

CraftingPlan CraftingSimulationState::build_plan(
        ResourceStack final_output, bool simulation,
        bool multiple_paths) const {
    CraftingPlan plan;
    plan.final_output = final_output;
    plan.simulation = simulation;
    plan.multiple_paths = multiple_paths;
    plan.bytes = static_cast<int64_t>(std::ceil(bytes_));
    plan.used_items = required_extract_;
    plan.emitted_items = emitted_;
    plan.missing_items = missing_;
    plan.pattern_times = crafts_;
    return plan;
}

// ============================================================
// CraftingTreeNode
// ============================================================

CraftingTreeNode::CraftingTreeNode(ItemId what, int64_t amount,
                                   CraftingTreeProcess* parent_process,
                                   int input_slot)
    : what_(what)
    , amount_(amount)
    , parent_process_(parent_process)
    , input_slot_(input_slot) {}

void CraftingTreeNode::build_child_patterns() {
    if (patterns_built_) return;
    patterns_built_ = true;

    auto patterns = PatternRegistry::find_patterns_for(what_);
    for (const auto* pat : patterns) {
        if (not_recursive(pat)) {
            processes_.emplace_back(pat, this);
        }
    }
}

bool CraftingTreeNode::not_recursive(const AEPattern* pattern) const {
    auto pattern_outputs = pattern->get_outputs();
    ItemId pattern_output_id = pattern_outputs.empty()
        ? kInvalidItemId : pattern_outputs[0].item_id();

    const CraftingTreeProcess* pp = parent_process_;
    while (pp != nullptr) {
        auto parent_outputs = pp->pattern()->get_outputs();
        for (const auto& out : parent_outputs) {
            if (out.item_id() == pattern_output_id) return false;
        }
        auto parent_inputs = pp->pattern()->get_inputs();
        for (const auto& in : parent_inputs) {
            if (in.item_id() == pattern_output_id) return false;
        }
        pp = pp->parent_node() ? pp->parent_node()->parent_process() : nullptr;
    }
    return true;
}

void CraftingTreeNode::request(CraftingSimulationState& state,
                                int64_t requested_amount) {
    if (requested_amount <= 0) return;

    int64_t remaining = requested_amount;

    // Step 1: Try to extract from simulation inventory.
    int64_t extracted = state.extract(what_, remaining);
    remaining -= extracted;
    if (remaining <= 0) return;

    // Step 2: Try to emit.
    if (can_emit_) {
        state.emit_items(what_, remaining);
        state.add_bytes(remaining);
        return;
    }

    // Step 3: Try to craft using patterns.
    build_child_patterns();

    if (processes_.empty()) {
        throw CraftBranchFailure(what_, remaining);
    }

    if (processes_.size() == 1) {
        auto& proc = processes_[0];
        ResourceStack output = proc.pattern()->get_primary_output();
        int64_t per_craft = output.is_valid() && output.amount > 0 ? output.amount : 1;
        int64_t crafts_needed = (remaining + per_craft - 1) / per_craft;

        // Check for recursive inputs.
        bool has_recursive = false;
        for (const auto& input : proc.pattern()->get_inputs()) {
            if (input.item_id() == what_) { has_recursive = true; break; }
        }

        if (has_recursive || crafts_needed == 1) {
            // One at a time.
            for (int64_t i = 0; i < crafts_needed && remaining > 0; ++i) {
                CraftingSimulationState child;
                child.initialize(state.get_working());
                for (auto& [node, mult] : proc.mutable_nodes()) {
                    if (node.what() == what_) continue;
                    node.request(child, mult);
                }
                state.apply_diff(child);
                state.insert(what_, per_craft);
                state.add_crafting(proc.pattern(), 1);
                remaining -= std::min(per_craft, remaining);
            }
        } else {
            CraftingSimulationState child;
            child.initialize(state.get_working());
            for (auto& [node, mult] : proc.mutable_nodes()) {
                node.request(child, mult * crafts_needed);
            }
            state.apply_diff(child);
            state.insert(what_, per_craft * crafts_needed);
            state.add_crafting(proc.pattern(), crafts_needed);
        }
    } else {
        bool any_success = false;
        for (auto& proc : processes_) {
            if (!proc.is_possible()) continue;
            ResourceStack output = proc.pattern()->get_primary_output();
            int64_t per_craft = output.is_valid() && output.amount > 0 ? output.amount : 1;
            int64_t crafts_needed = (remaining + per_craft - 1) / per_craft;

            try {
                CraftingSimulationState child;
                child.initialize(state.get_working());
                for (auto& [node, mult] : proc.mutable_nodes()) {
                    node.request(child, mult * crafts_needed);
                }
                state.apply_diff(child);
                state.insert(what_, per_craft * crafts_needed);
                state.add_crafting(proc.pattern(), crafts_needed);
                any_success = true;
                break;
            } catch (const CraftBranchFailure&) {
                proc.set_possible(true);
            }
        }
        if (!any_success) {
            throw CraftBranchFailure(what_, remaining);
        }
    }
}

int64_t CraftingTreeNode::get_node_count() const {
    int64_t count = 1;
    for (const auto& proc : processes_) {
        count += proc.get_node_count();
    }
    return count;
}

bool CraftingTreeNode::has_multiple_paths() const {
    if (processes_.size() > 1) return true;
    for (const auto& proc : processes_) {
        if (proc.has_multiple_paths()) return true;
    }
    return false;
}

CraftingTreeProcess* CraftingTreeNode::parent_process() { return parent_process_; }
const CraftingTreeProcess* CraftingTreeNode::parent_process() const { return parent_process_; }
int CraftingTreeNode::input_slot() const { return input_slot_; }

// ============================================================
// CraftingTreeProcess
// ============================================================

CraftingTreeProcess::CraftingTreeProcess(const AEPattern* pattern,
                                         CraftingTreeNode* parent_node)
    : pattern_(pattern), parent_node_(parent_node) {
    auto inputs = pattern->get_inputs();
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto& input = inputs[i];
        if (input.is_valid()) {
            nodes_.emplace_back(
                CraftingTreeNode(input.item_id(), input.amount, this, (int)i),
                input.amount);
        }
    }
}

void CraftingTreeProcess::request(CraftingSimulationState& state, int64_t times) {
    if (times <= 0) return;

    for (auto& [node, multiplier] : nodes_) {
        node.request(state, multiplier * times);
    }

    auto outputs = pattern_->get_outputs();
    for (auto& output : outputs) {
        if (output.is_valid()) {
            state.insert(output.item_id(), output.amount * times);
        }
    }

    state.add_crafting(pattern_, times);
    state.add_bytes(times * static_cast<double>(pattern_->get_inputs().size()));
}

int64_t CraftingTreeProcess::get_node_count() const {
    int64_t count = 1;
    for (const auto& [node, _] : nodes_) {
        count += node.get_node_count();
    }
    return count;
}

bool CraftingTreeProcess::has_multiple_paths() const {
    for (const auto& [node, _] : nodes_) {
        if (node.has_multiple_paths()) return true;
    }
    return false;
}

const std::vector<std::pair<CraftingTreeNode, int64_t>>& CraftingTreeProcess::child_nodes() const { return nodes_; }
std::vector<std::pair<CraftingTreeNode, int64_t>>& CraftingTreeProcess::mutable_nodes() { return nodes_; }
CraftingTreeNode* CraftingTreeProcess::parent_node() { return parent_node_; }
const CraftingTreeNode* CraftingTreeProcess::parent_node() const { return parent_node_; }

// ============================================================
// CraftingCalculation
// ============================================================

void CraftingCalculation::initialize(ItemId what, int64_t amount,
                                     CraftingSimulationState&& network_state) {
    what_ = what;
    requested_amount_ = amount;
    root_ = CraftingTreeNode(what, amount, nullptr, -1);
    // Store the base snapshot for reset between attempts.
    base_snapshot_ = network_state.get_working();
    sim_state_ = std::move(network_state);
    root_.set_can_emit(PatternRegistry::is_emitable(what));
    missing_.clear();
    multi_paths_ = false;
}

CraftingPlan CraftingCalculation::compute() {
    CraftingPlan plan = run_attempt(requested_amount_, false);
    if (plan.is_valid()) {
        multi_paths_ = root_.has_multiple_paths();
        return plan;
    }

    // CRAFT_LESS binary search.
    int64_t low = 0;
    int64_t high = requested_amount_;
    int64_t best = 0;
    CraftingPlan best_plan;

    while (low < high) {
        int64_t mid = low + (high - low + 1) / 2;
        CraftingPlan attempt = run_attempt(mid, false);
        if (attempt.is_valid()) {
            best = mid;
            best_plan = attempt;
            low = mid;
        } else {
            high = mid - 1;
        }
    }

    if (best > 0) {
        return best_plan;
    }

    plan = run_attempt(requested_amount_, true);
    multi_paths_ = root_.has_multiple_paths();
    return plan;
}

CraftingPlan CraftingCalculation::run_attempt(int64_t amount, bool simulate) {
    CraftingCalculation calc;
    calc.what_ = what_;
    calc.requested_amount_ = amount;
    calc.base_snapshot_ = base_snapshot_;
    calc.root_ = CraftingTreeNode(what_, amount, nullptr, -1);
    calc.root_.set_can_emit(PatternRegistry::is_emitable(what_));

    CraftingSimulationState fresh;
    fresh.initialize(base_snapshot_);

    try {
        calc.root_.request(fresh, amount);
    } catch (const CraftBranchFailure& e) {
        if (!simulate) {
            return CraftingPlan{};
        }
        fresh.mark_missing(e.failed_item(), e.how_many());
    }

    return fresh.build_plan(
        ResourceStack::item(what_, amount),
        simulate, false);
}

} // namespace science_and_theology::gt
