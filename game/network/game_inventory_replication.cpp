// Authenticated-player inventory replication codec and cache implementation.

#include "game/network/game_inventory_replication.h"

#include "core/error.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::byte>& bytes, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(uint32_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(uint64_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if ((require_non_empty && value.empty()) || has_embedded_nul(value) ||
        value.size() > maximum || value.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error(std::string("Player inventory ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char byte : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Player inventory ") + field_name + " is truncated");
    }
    const size_t size = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && size == 0) || size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Player inventory ") + field_name + " is invalid");
    }
    std::string value;
    value.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += size;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("Player inventory ") + field_name + " contains a NUL byte");
    }
    return value;
}

[[nodiscard]] bool is_empty_stack(const GamePlayerItemStack& stack) noexcept {
    return stack.item_id.empty() && stack.count == 0 && stack.instance_data.empty();
}

[[nodiscard]] snt::core::Expected<void> validate_stack(
    const GamePlayerItemStack& stack, int32_t max_stack_size, bool allow_empty,
    const char* field_name) {
    if (is_empty_stack(stack)) {
        if (allow_empty) return {};
        return protocol_error(std::string("Player inventory ") + field_name + " must not be empty");
    }
    if (stack.item_id.empty() || stack.item_id.size() > kMaxGameItemIdBytes ||
        stack.instance_data.size() > kMaxGameInventoryItemInstanceBytes ||
        has_embedded_nul(stack.item_id) || has_embedded_nul(stack.instance_data) ||
        stack.count <= 0 || stack.count > max_stack_size ||
        (!stack.instance_data.empty() && stack.count != 1)) {
        return protocol_error(std::string("Player inventory ") + field_name + " is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_response(
    const GameInventoryCommandResponse& response, bool allow_none) {
    if (response.request_id == 0) {
        if (allow_none && response.kind == GameInventoryCommandKind::kNone &&
            response.outcome == GameInventorySlotTransferOutcome::kNone &&
            response.rejection_reason.empty()) {
            return {};
        }
        return protocol_error("Player inventory transfer response has no request id");
    }
    if (response.kind != GameInventoryCommandKind::kInventorySlotTransfer &&
        response.kind != GameInventoryCommandKind::kMachineInputSlotTransfer) {
        return protocol_error("Player inventory command response kind is invalid");
    }
    if (response.outcome != GameInventorySlotTransferOutcome::kAccepted &&
        response.outcome != GameInventorySlotTransferOutcome::kRejected) {
        return protocol_error("Player inventory transfer response outcome is invalid");
    }
    if (response.rejection_reason.size() > kMaxGameInventoryResponseReasonBytes ||
        has_embedded_nul(response.rejection_reason) ||
        (response.outcome == GameInventorySlotTransferOutcome::kAccepted &&
         !response.rejection_reason.empty())) {
        return protocol_error("Player inventory transfer response reason is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_inventory(
    const GamePlayerInventory& inventory) {
    if (inventory.max_slots == 0 || inventory.max_slots > kMaxGameInventorySlots ||
        inventory.slots.size() != inventory.max_slots || inventory.max_stack_size <= 0 ||
        inventory.max_stack_size > kMaxGameInventoryStackSize) {
        return protocol_error("Player inventory layout is invalid");
    }
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (auto result = validate_stack(stack, inventory.max_stack_size, true, "stack"); !result) {
            return result.error();
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_stack(
    std::vector<std::byte>& bytes, const GamePlayerItemStack& stack, int32_t max_stack_size) {
    if (auto result = validate_stack(stack, max_stack_size, true, "stack"); !result) {
        return result.error();
    }
    if (is_empty_stack(stack)) {
        bytes.push_back(std::byte{0});
        return {};
    }
    bytes.push_back(std::byte{1});
    if (auto result = append_short_string(bytes, stack.item_id, kMaxGameItemIdBytes, "item id", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, stack.count);
    return append_short_string(bytes, stack.instance_data, kMaxGameInventoryItemInstanceBytes,
                               "item instance data", false);
}

[[nodiscard]] snt::core::Expected<GamePlayerItemStack> read_stack(
    std::span<const std::byte> bytes, size_t& offset, int32_t max_stack_size) {
    if (offset >= bytes.size()) return protocol_error("Player inventory stack is truncated");
    const uint8_t present = std::to_integer<uint8_t>(bytes[offset++]);
    if (present == 0) return GamePlayerItemStack{};
    if (present != 1) return protocol_error("Player inventory stack presence flag is invalid");
    auto item_id = read_short_string(bytes, offset, kMaxGameItemIdBytes, "item id", true);
    if (!item_id) return item_id.error();
    if (bytes.size() - offset < sizeof(uint32_t)) {
        return protocol_error("Player inventory stack count is truncated");
    }
    const int32_t count = read_i32(bytes, offset);
    offset += sizeof(uint32_t);
    auto instance_data = read_short_string(bytes, offset, kMaxGameInventoryItemInstanceBytes,
                                           "item instance data", false);
    if (!instance_data) return instance_data.error();
    GamePlayerItemStack stack{
        .item_id = std::move(*item_id),
        .count = count,
        .instance_data = std::move(*instance_data),
    };
    if (auto result = validate_stack(stack, max_stack_size, false, "stack"); !result) {
        return result.error();
    }
    return stack;
}

[[nodiscard]] snt::core::Expected<void> append_response(
    std::vector<std::byte>& bytes, const GameInventoryCommandResponse& response,
    bool allow_none) {
    if (auto result = validate_response(response, allow_none); !result) return result.error();
    append_u64(bytes, response.request_id);
    bytes.push_back(static_cast<std::byte>(response.kind));
    bytes.push_back(static_cast<std::byte>(response.outcome));
    return append_short_string(bytes, response.rejection_reason,
                               kMaxGameInventoryResponseReasonBytes,
                               "transfer response reason", false);
}

[[nodiscard]] snt::core::Expected<GameInventoryCommandResponse> read_response(
    std::span<const std::byte> bytes, size_t& offset, bool allow_none) {
    if (bytes.size() - offset < sizeof(uint64_t) + sizeof(uint8_t) * 2) {
        return protocol_error("Player inventory transfer response is truncated");
    }
    GameInventoryCommandResponse response;
    response.request_id = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    response.kind = static_cast<GameInventoryCommandKind>(
        std::to_integer<uint8_t>(bytes[offset++]));
    response.outcome = static_cast<GameInventorySlotTransferOutcome>(
        std::to_integer<uint8_t>(bytes[offset++]));
    auto reason = read_short_string(bytes, offset, kMaxGameInventoryResponseReasonBytes,
                                    "transfer response reason", false);
    if (!reason) return reason.error();
    response.rejection_reason = std::move(*reason);
    if (auto result = validate_response(response, allow_none); !result) return result.error();
    return response;
}

[[nodiscard]] snt::core::Expected<void> validate_snapshot(const GameInventorySnapshot& snapshot) {
    if (snapshot.account_id.empty() || snapshot.account_id.size() > kMaxGamePlayerIdBytes ||
        has_embedded_nul(snapshot.account_id) || snapshot.inventory_revision == 0 ||
        (snapshot.response_revision == 0 && snapshot.response.request_id != 0) ||
        (snapshot.response_revision != 0 && snapshot.response.request_id == 0)) {
        return protocol_error("Player inventory snapshot header is invalid");
    }
    if (auto result = validate_response(snapshot.response, true); !result) return result.error();
    return validate_inventory(snapshot.inventory);
}

[[nodiscard]] snt::core::Expected<void> validate_delta(const GameInventoryDelta& delta) {
    if (delta.account_id.empty() || delta.account_id.size() > kMaxGamePlayerIdBytes ||
        has_embedded_nul(delta.account_id) || delta.inventory_revision == 0 ||
        delta.changed_slots.size() > kMaxGameInventorySlots) {
        return protocol_error("Player inventory delta header is invalid");
    }
    if (auto result = validate_response(delta.response, true); !result) return result.error();
    std::vector<bool> seen(kMaxGameInventorySlots, false);
    for (const GameInventorySlotChange& change : delta.changed_slots) {
        if (change.slot_index >= kMaxGameInventorySlots || seen[change.slot_index]) {
            return protocol_error("Player inventory delta slot record is invalid or duplicated");
        }
        seen[change.slot_index] = true;
        if (auto result = validate_stack(change.stack, kMaxGameInventoryStackSize, true,
                                         "delta stack");
            !result) {
            return result.error();
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_payload_size(const std::vector<std::byte>& payload) {
    if (payload.empty() || payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Player inventory replication payload exceeds the configured limit");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<GameInventorySnapshot> read_snapshot(
    std::span<const std::byte> bytes, size_t& offset) {
    auto account_id = read_short_string(bytes, offset, kMaxGamePlayerIdBytes, "account id", true);
    if (!account_id) return account_id.error();
    if (bytes.size() - offset < sizeof(uint64_t) * 2) {
        return protocol_error("Player inventory snapshot header is truncated");
    }
    GameInventorySnapshot snapshot;
    snapshot.account_id = std::move(*account_id);
    snapshot.inventory_revision = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    snapshot.response_revision = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    auto response = read_response(bytes, offset, true);
    if (!response) return response.error();
    snapshot.response = std::move(*response);
    if (bytes.size() - offset < sizeof(uint16_t) + sizeof(uint32_t)) {
        return protocol_error("Player inventory snapshot layout is truncated");
    }
    snapshot.inventory.max_slots = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    snapshot.inventory.max_stack_size = read_i32(bytes, offset);
    offset += sizeof(uint32_t);
    if (snapshot.inventory.max_slots == 0 || snapshot.inventory.max_slots > kMaxGameInventorySlots ||
        snapshot.inventory.max_stack_size <= 0 ||
        snapshot.inventory.max_stack_size > kMaxGameInventoryStackSize) {
        return protocol_error("Player inventory snapshot layout is invalid");
    }
    snapshot.inventory.slots.reserve(snapshot.inventory.max_slots);
    for (uint32_t index = 0; index < snapshot.inventory.max_slots; ++index) {
        auto stack = read_stack(bytes, offset, snapshot.inventory.max_stack_size);
        if (!stack) return stack.error();
        snapshot.inventory.slots.push_back(std::move(*stack));
    }
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    return snapshot;
}

[[nodiscard]] snt::core::Expected<GameInventoryDelta> read_delta(
    std::span<const std::byte> bytes, size_t& offset) {
    auto account_id = read_short_string(bytes, offset, kMaxGamePlayerIdBytes, "account id", true);
    if (!account_id) return account_id.error();
    if (bytes.size() - offset < sizeof(uint64_t) * 2) {
        return protocol_error("Player inventory delta header is truncated");
    }
    GameInventoryDelta delta;
    delta.account_id = std::move(*account_id);
    delta.inventory_revision = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    delta.response_revision = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    auto response = read_response(bytes, offset, true);
    if (!response) return response.error();
    delta.response = std::move(*response);
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Player inventory delta slot count is truncated");
    }
    const uint16_t change_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (change_count > kMaxGameInventorySlots) {
        return protocol_error("Player inventory delta has too many slot changes");
    }
    delta.changed_slots.reserve(change_count);
    for (uint16_t index = 0; index < change_count; ++index) {
        if (bytes.size() - offset < sizeof(uint16_t)) {
            return protocol_error("Player inventory delta slot index is truncated");
        }
        const uint16_t slot_index = read_u16(bytes, offset);
        offset += sizeof(uint16_t);
        auto stack = read_stack(bytes, offset, kMaxGameInventoryStackSize);
        if (!stack) return stack.error();
        delta.changed_slots.push_back({.slot_index = slot_index, .stack = std::move(*stack)});
    }
    if (auto result = validate_delta(delta); !result) return result.error();
    return delta;
}

}  // namespace

snt::core::Expected<std::vector<std::byte>> encode_game_inventory_snapshot(
    const GameInventorySnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    std::vector<std::byte> payload;
    payload.reserve(128 + snapshot.inventory.slots.size() * 12);
    payload.push_back(static_cast<std::byte>(kGameInventoryReplicationVersion));
    payload.push_back(static_cast<std::byte>(GameInventoryReplicationPayloadKind::kSnapshot));
    if (auto result = append_short_string(payload, snapshot.account_id, kMaxGamePlayerIdBytes,
                                          "account id", true);
        !result) {
        return result.error();
    }
    append_u64(payload, snapshot.inventory_revision);
    append_u64(payload, snapshot.response_revision);
    if (auto result = append_response(payload, snapshot.response, true); !result) return result.error();
    append_u16(payload, static_cast<uint16_t>(snapshot.inventory.max_slots));
    append_i32(payload, snapshot.inventory.max_stack_size);
    for (const GamePlayerItemStack& stack : snapshot.inventory.slots) {
        if (auto result = append_stack(payload, stack, snapshot.inventory.max_stack_size); !result) {
            return result.error();
        }
    }
    if (auto result = validate_payload_size(payload); !result) return result.error();
    return payload;
}

snt::core::Expected<std::vector<std::byte>> encode_game_inventory_delta(
    const GameInventoryDelta& delta) {
    if (auto result = validate_delta(delta); !result) return result.error();
    std::vector<std::byte> payload;
    payload.reserve(96 + delta.changed_slots.size() * 12);
    payload.push_back(static_cast<std::byte>(kGameInventoryReplicationVersion));
    payload.push_back(static_cast<std::byte>(GameInventoryReplicationPayloadKind::kDelta));
    if (auto result = append_short_string(payload, delta.account_id, kMaxGamePlayerIdBytes,
                                          "account id", true);
        !result) {
        return result.error();
    }
    append_u64(payload, delta.inventory_revision);
    append_u64(payload, delta.response_revision);
    if (auto result = append_response(payload, delta.response, true); !result) return result.error();
    append_u16(payload, static_cast<uint16_t>(delta.changed_slots.size()));
    for (const GameInventorySlotChange& change : delta.changed_slots) {
        append_u16(payload, change.slot_index);
        if (auto result = append_stack(payload, change.stack, kMaxGameInventoryStackSize); !result) {
            return result.error();
        }
    }
    if (auto result = validate_payload_size(payload); !result) return result.error();
    return payload;
}

snt::core::Expected<GameInventoryReplicationPayload>
decode_game_inventory_replication_payload(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(uint8_t) * 2 ||
        std::to_integer<uint8_t>(payload[0]) != kGameInventoryReplicationVersion) {
        return protocol_error("Player inventory replication version is invalid");
    }
    size_t offset = sizeof(uint8_t) * 2;
    switch (static_cast<GameInventoryReplicationPayloadKind>(std::to_integer<uint8_t>(payload[1]))) {
        case GameInventoryReplicationPayloadKind::kSnapshot: {
            auto snapshot = read_snapshot(payload, offset);
            if (!snapshot) return snapshot.error();
            if (offset != payload.size()) {
                return protocol_error("Player inventory snapshot has trailing bytes");
            }
            return GameInventoryReplicationPayload{std::move(*snapshot)};
        }
        case GameInventoryReplicationPayloadKind::kDelta: {
            auto delta = read_delta(payload, offset);
            if (!delta) return delta.error();
            if (offset != payload.size()) {
                return protocol_error("Player inventory delta has trailing bytes");
            }
            return GameInventoryReplicationPayload{std::move(*delta)};
        }
    }
    return protocol_error("Player inventory replication payload kind is invalid");
}

GameClientInventoryState::GameClientInventoryState(std::string local_account_id)
    : local_account_id_(std::move(local_account_id)) {}

snt::core::Expected<void> GameClientInventoryState::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Client inventory state received an invalid snapshot id");
    }
    const GameReplicationValue* inventory = nullptr;
    for (const GameReplicationValue& value : snapshot.values) {
        if (value.kind != GameReplicationValueKind::kPlayerInventory) continue;
        if (inventory != nullptr || value.operation != GameReplicationValueOperation::kUpsert) {
            return protocol_error("Client inventory snapshot has an invalid value record");
        }
        inventory = &value;
    }

    std::optional<GameInventorySnapshot> next;
    if (inventory != nullptr) {
        auto payload = decode_game_inventory_replication_payload(inventory->payload);
        if (!payload) return payload.error();
        if (!std::holds_alternative<GameInventorySnapshot>(*payload)) {
            return protocol_error("Client inventory initial value is not a full snapshot");
        }
        GameInventorySnapshot decoded = std::get<GameInventorySnapshot>(std::move(*payload));
        if (decoded.account_id != local_account_id_) {
            return protocol_error("Client inventory snapshot belongs to a different account");
        }
        next = std::move(decoded);
    }
    snapshot_ = std::move(next);
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameClientInventoryState::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Client inventory delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Client inventory delta sequence is invalid");
    }
    const GameReplicationValue* inventory = nullptr;
    for (const GameReplicationValue& value : delta.values) {
        if (value.kind != GameReplicationValueKind::kPlayerInventory) continue;
        if (inventory != nullptr) {
            return protocol_error("Client inventory delta has duplicate value records");
        }
        inventory = &value;
    }
    if (inventory != nullptr) {
        switch (inventory->operation) {
            case GameReplicationValueOperation::kUpsert: {
                auto payload = decode_game_inventory_replication_payload(inventory->payload);
                if (!payload) return payload.error();
                auto applied = std::visit(
                    [this](const auto& value) -> snt::core::Expected<void> {
                        using Value = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<Value, GameInventorySnapshot>) {
                            return apply_snapshot_value(value, true);
                        } else {
                            return apply_delta_value(value);
                        }
                    },
                    *payload);
                if (!applied) return applied.error();
                break;
            }
            case GameReplicationValueOperation::kRemove:
                if (!inventory->payload.empty()) {
                    return protocol_error("Client inventory remove carries a payload");
                }
                snapshot_.reset();
                break;
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

void GameClientInventoryState::clear() noexcept {
    snapshot_.reset();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameClientInventoryState::apply_snapshot_value(
    const GameInventorySnapshot& snapshot, bool require_newer_revision) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    if (snapshot.account_id != local_account_id_) {
        return protocol_error("Client inventory snapshot belongs to a different account");
    }
    if (require_newer_revision && snapshot_) {
        if (snapshot.inventory_revision < snapshot_->inventory_revision ||
            (snapshot.inventory_revision == snapshot_->inventory_revision &&
             snapshot.response_revision <= snapshot_->response_revision)) {
            return protocol_error("Client inventory snapshot did not advance state");
        }
    }
    snapshot_ = snapshot;
    return {};
}

snt::core::Expected<void> GameClientInventoryState::apply_delta_value(
    const GameInventoryDelta& delta) {
    if (auto result = validate_delta(delta); !result) return result.error();
    if (!snapshot_) return protocol_error("Client inventory delta arrived before its snapshot");
    if (delta.account_id != local_account_id_) {
        return protocol_error("Client inventory delta belongs to a different account");
    }
    if (delta.inventory_revision < snapshot_->inventory_revision ||
        delta.response_revision < snapshot_->response_revision) {
        return protocol_error("Client inventory delta regressed a revision");
    }
    if (delta.inventory_revision == snapshot_->inventory_revision && !delta.changed_slots.empty()) {
        return protocol_error("Client inventory delta changed slots without a new inventory revision");
    }
    if (delta.response_revision == snapshot_->response_revision && delta.response.request_id != 0) {
        return protocol_error("Client inventory delta repeated a transfer response");
    }
    if (delta.response_revision > snapshot_->response_revision && delta.response.request_id == 0) {
        return protocol_error("Client inventory delta advanced response without a transfer response");
    }

    GameInventorySnapshot candidate = *snapshot_;
    for (const GameInventorySlotChange& change : delta.changed_slots) {
        if (change.slot_index >= candidate.inventory.slots.size()) {
            return protocol_error("Client inventory delta references an unavailable slot");
        }
        if (auto result = validate_stack(change.stack, candidate.inventory.max_stack_size, true,
                                         "delta stack");
            !result) {
            return result.error();
        }
        candidate.inventory.slots[change.slot_index] = change.stack;
    }
    candidate.inventory_revision = delta.inventory_revision;
    if (delta.response_revision > candidate.response_revision) {
        candidate.response_revision = delta.response_revision;
        candidate.response = delta.response;
    }
    if (auto result = validate_snapshot(candidate); !result) return result.error();
    snapshot_ = std::move(candidate);
    return {};
}

}  // namespace snt::game::replication
