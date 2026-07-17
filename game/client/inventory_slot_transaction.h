// Game-owned inventory slot transaction boundary.
//
// This module is intentionally independent from Retained-MUI, ECS, and the
// replication transport. A UI submits only stable slot indices and value
// snapshots; an authority later returns a complete value confirmation. The
// local authority is a deterministic offline simulator, while a network
// adapter can implement the same command/confirmation interfaces.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::game {

// A game-owned presentation stack. `instance_data` keeps the transaction
// contract ready for singular durability/custom-state items without exposing
// item objects or ECS handles to UI code.
struct ItemStackState {
    std::string item_key;
    int32_t count = 0;
    std::string instance_data;

    [[nodiscard]] bool empty() const noexcept {
        return item_key.empty() || count <= 0;
    }

    friend bool operator==(const ItemStackState&, const ItemStackState&) = default;
};

struct InventoryState {
    std::vector<ItemStackState> slots;
    int32_t columns = 9;
    int32_t selected_hotbar = 0;
    int32_t max_stack_size = 64;
};

// A transaction always carries the source and target values observed by the
// client. The authoritative side rejects stale slots instead of moving a
// different item that arrived after the drag began.
struct InventorySlotTransferRequest {
    uint64_t request_id = 0;
    uint64_t expected_revision = 0;
    uint32_t source_slot = 0;
    uint32_t target_slot = 0;
    int32_t count = 0;
    ItemStackState expected_source;
    ItemStackState expected_target;
};

enum class InventorySlotTransferOutcome : uint8_t {
    Accepted,
    Rejected,
};

// Every confirmation carries the authority's complete current slot snapshot.
// Rejections can therefore repair stale presentation state without an
// optimistic UI rollback path.
struct InventorySlotTransferConfirmation {
    uint64_t request_id = 0;
    InventorySlotTransferOutcome outcome = InventorySlotTransferOutcome::Rejected;
    uint64_t authoritative_revision = 0;
    std::vector<ItemStackState> slots;
    int32_t max_stack_size = 64;
    std::string rejection_reason;
};

// The command channel owns transport/simulation scheduling. Returning success
// only means the request was accepted for processing; the UI must wait for a
// matching confirmation before changing its InventoryViewModel.
class IInventorySlotTransferCommandSink {
public:
    virtual ~IInventorySlotTransferCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_slot_transfer(
        InventorySlotTransferRequest request) = 0;
};

// A local simulation or network adapter exposes confirmed transactions through
// this pull boundary. The client session owns polling and forwards each value
// to GameplayUiController; the UI never retains the source implementation.
class IInventorySlotTransferConfirmationSource {
public:
    virtual ~IInventorySlotTransferConfirmationSource() = default;

    [[nodiscard]] virtual std::vector<InventorySlotTransferConfirmation>
    drain_slot_transfer_confirmations() = 0;
};

// Deterministic transfer rules shared by the offline authority and unit tests.
// Empty targets receive a move/split, matching normal stacks merge up to the
// configured capacity, and a full source can swap with a different target.
[[nodiscard]] snt::core::Expected<InventoryState> apply_inventory_slot_transfer(
    InventoryState inventory,
    const InventorySlotTransferRequest& request);

// Offline sessions use this implementation as their simulation authority. It
// deliberately queues confirmations so callers exercise the same request /
// confirmation lifecycle as a future network-backed implementation.
class LocalInventorySlotTransferAuthority final
    : public IInventorySlotTransferCommandSink,
      public IInventorySlotTransferConfirmationSource {
public:
    explicit LocalInventorySlotTransferAuthority(InventoryState initial_state,
                                                  uint64_t initial_revision = 0);

    [[nodiscard]] snt::core::Expected<void> submit_slot_transfer(
        InventorySlotTransferRequest request) override;
    [[nodiscard]] std::vector<InventorySlotTransferConfirmation>
    drain_slot_transfer_confirmations() override;

    // Offline gameplay systems such as the current craft demo can commit
    // state outside slot dragging. The session synchronizes this simulator at
    // that explicit boundary; network authorities must never implement it.
    [[nodiscard]] snt::core::Expected<void> synchronize_offline_snapshot(
        InventoryState state,
        uint64_t authoritative_revision);

    [[nodiscard]] const InventoryState& state() const noexcept { return state_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] InventorySlotTransferConfirmation make_confirmation(
        uint64_t request_id,
        InventorySlotTransferOutcome outcome,
        std::string rejection_reason = {}) const;

    InventoryState state_;
    uint64_t revision_ = 0;
    std::vector<InventorySlotTransferConfirmation> confirmations_;
};

}  // namespace snt::game
