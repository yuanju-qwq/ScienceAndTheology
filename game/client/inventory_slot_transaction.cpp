// Game-owned deterministic inventory slot transaction implementation.

#include "inventory_slot_transaction.h"

#include "core/error.h"

#include <limits>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_empty_stack(const ItemStackState& stack) noexcept {
    return stack.item_key.empty() && stack.count == 0 && stack.instance_data.empty();
}

void clear_stack(ItemStackState& stack) noexcept {
    stack.item_key.clear();
    stack.count = 0;
    stack.instance_data.clear();
}

[[nodiscard]] bool has_valid_nonempty_stack(const ItemStackState& stack) noexcept {
    return !stack.item_key.empty() && stack.count > 0 &&
        (stack.instance_data.empty() || stack.count == 1);
}

[[nodiscard]] snt::core::Expected<void> validate_inventory_state(
    const InventoryState& inventory) {
    if (inventory.columns <= 0 || inventory.max_stack_size <= 0) {
        return invalid_argument("Inventory slot transaction has invalid layout or stack limits");
    }
    for (const ItemStackState& stack : inventory.slots) {
        if (is_empty_stack(stack)) continue;
        if (!has_valid_nonempty_stack(stack) || stack.count > inventory.max_stack_size) {
            return invalid_argument("Inventory slot transaction received an invalid stack snapshot");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_request_shape(
    const InventorySlotTransferRequest& request) {
    if (request.request_id == 0 || request.count <= 0 ||
        request.source_slot == request.target_slot) {
        return invalid_argument("Inventory slot transfer request has an invalid identity or count");
    }
    if (!has_valid_nonempty_stack(request.expected_source)) {
        return invalid_argument("Inventory slot transfer request has an invalid expected source");
    }
    if (!is_empty_stack(request.expected_target) &&
        !has_valid_nonempty_stack(request.expected_target)) {
        return invalid_argument("Inventory slot transfer request has an invalid expected target");
    }
    return {};
}

[[nodiscard]] bool stacks_can_merge(const ItemStackState& left,
                                    const ItemStackState& right) noexcept {
    return !left.item_key.empty() && left.item_key == right.item_key &&
        left.instance_data.empty() && right.instance_data.empty();
}

}  // namespace

snt::core::Expected<InventoryState> apply_inventory_slot_transfer(
    InventoryState inventory,
    const InventorySlotTransferRequest& request) {
    if (auto result = validate_inventory_state(inventory); !result) return result.error();
    if (auto result = validate_request_shape(request); !result) return result.error();
    if (request.source_slot >= inventory.slots.size() ||
        request.target_slot >= inventory.slots.size()) {
        return invalid_argument("Inventory slot transfer references a slot outside the snapshot");
    }

    ItemStackState& source = inventory.slots[request.source_slot];
    ItemStackState& target = inventory.slots[request.target_slot];
    if (source != request.expected_source || target != request.expected_target) {
        return invalid_state("Inventory slot transfer was rejected because its observed slots are stale");
    }
    if (request.count > source.count) {
        return invalid_argument("Inventory slot transfer count exceeds the source stack");
    }

    if (is_empty_stack(target)) {
        target = source;
        target.count = request.count;
        source.count -= request.count;
        if (source.count == 0) clear_stack(source);
    } else if (stacks_can_merge(source, target)) {
        if (target.count > inventory.max_stack_size - request.count) {
            return invalid_state("Inventory slot transfer does not fit the target stack");
        }
        target.count += request.count;
        source.count -= request.count;
        if (source.count == 0) clear_stack(source);
    } else {
        if (request.count != source.count) {
            return invalid_state("Inventory slot transfer can swap only a complete source stack");
        }
        std::swap(source, target);
    }

    if (auto result = validate_inventory_state(inventory); !result) return result.error();
    return inventory;
}

LocalInventorySlotTransferAuthority::LocalInventorySlotTransferAuthority(
    InventoryState initial_state,
    uint64_t initial_revision)
    : state_(std::move(initial_state)), revision_(initial_revision) {}

snt::core::Expected<void> LocalInventorySlotTransferAuthority::submit_slot_transfer(
    InventorySlotTransferRequest request) {
    if (request.request_id == 0) {
        return invalid_argument("Inventory slot transfer command has no request id");
    }
    if (request.expected_revision != revision_) {
        confirmations_.push_back(make_confirmation(
            request.request_id, InventorySlotTransferOutcome::Rejected,
            "inventory revision is stale"));
        return {};
    }
    if (revision_ == std::numeric_limits<uint64_t>::max()) {
        confirmations_.push_back(make_confirmation(
            request.request_id, InventorySlotTransferOutcome::Rejected,
            "inventory revision sequence is exhausted"));
        return {};
    }

    auto candidate = apply_inventory_slot_transfer(state_, request);
    if (!candidate) {
        confirmations_.push_back(make_confirmation(
            request.request_id, InventorySlotTransferOutcome::Rejected,
            candidate.error().message()));
        return {};
    }

    state_ = std::move(*candidate);
    ++revision_;
    confirmations_.push_back(make_confirmation(
        request.request_id, InventorySlotTransferOutcome::Accepted));
    return {};
}

std::vector<InventorySlotTransferConfirmation>
LocalInventorySlotTransferAuthority::drain_slot_transfer_confirmations() {
    std::vector<InventorySlotTransferConfirmation> result = std::move(confirmations_);
    confirmations_.clear();
    return result;
}

snt::core::Expected<void> LocalInventorySlotTransferAuthority::synchronize_offline_snapshot(
    InventoryState state,
    uint64_t authoritative_revision) {
    if (!confirmations_.empty()) {
        return invalid_state("Cannot synchronize offline inventory while confirmations are pending");
    }
    if (auto result = validate_inventory_state(state); !result) return result.error();
    state_ = std::move(state);
    revision_ = authoritative_revision;
    return {};
}

InventorySlotTransferConfirmation LocalInventorySlotTransferAuthority::make_confirmation(
    uint64_t request_id,
    InventorySlotTransferOutcome outcome,
    std::string rejection_reason) const {
    return {
        .request_id = request_id,
        .outcome = outcome,
        .authoritative_revision = revision_,
        .slots = state_.slots,
        .max_stack_size = state_.max_stack_size,
        .rejection_reason = std::move(rejection_reason),
    };
}

}  // namespace snt::game
