// Authenticated-player inventory replication values.
//
// Ownership: this module moves a player's own fixed-slot inventory through
// the SNTG presentation boundary. Initial snapshots carry the complete state;
// later deltas carry only changed slot values and a command acknowledgement.
// It never exposes another player's inventory or grants client mutation
// authority.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameInventoryReplicationVersion = 1;
inline constexpr size_t kMaxGameInventoryResponseReasonBytes = 512;

enum class GameInventoryReplicationPayloadKind : uint8_t {
    kSnapshot = 1,
    kDelta = 2,
};

enum class GameInventorySlotTransferOutcome : uint8_t {
    kNone = 0,
    kAccepted = 1,
    kRejected = 2,
};

struct GameInventorySlotTransferResponse {
    uint64_t request_id = 0;
    GameInventorySlotTransferOutcome outcome = GameInventorySlotTransferOutcome::kNone;
    std::string rejection_reason;
};

struct GameInventorySnapshot {
    std::string account_id;
    uint64_t inventory_revision = 0;
    uint64_t response_revision = 0;
    GameInventorySlotTransferResponse response;
    GamePlayerInventory inventory;
};

struct GameInventorySlotChange {
    uint16_t slot_index = 0;
    GamePlayerItemStack stack;
};

struct GameInventoryDelta {
    std::string account_id;
    uint64_t inventory_revision = 0;
    uint64_t response_revision = 0;
    // A response is present only when response_revision advances. The prior
    // response remains observable for otherwise unrelated inventory deltas.
    GameInventorySlotTransferResponse response;
    // A revision-only delta is valid when multiple host mutations collapse
    // back to identical slots before this observer's next reliable batch.
    // It still invalidates stale client transfer requests without resending
    // an unchanged inventory.
    std::vector<GameInventorySlotChange> changed_slots;
};

using GameInventoryReplicationPayload = std::variant<GameInventorySnapshot, GameInventoryDelta>;

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_game_inventory_snapshot(
    const GameInventorySnapshot& snapshot);
[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_game_inventory_delta(
    const GameInventoryDelta& delta);
[[nodiscard]] snt::core::Expected<GameInventoryReplicationPayload>
decode_game_inventory_replication_payload(std::span<const std::byte> payload);

// The graphical client owns this cache. It validates account ownership,
// snapshot/delta order, and changed slot bounds before exposing a value copy to
// UI code. It deliberately has no transport, ECS, or mutable inventory API.
class GameClientInventoryState final {
public:
    explicit GameClientInventoryState(std::string local_account_id);

    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] const GameInventorySnapshot* snapshot() const noexcept {
        return snapshot_ ? &*snapshot_ : nullptr;
    }
    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> apply_snapshot_value(
        const GameInventorySnapshot& snapshot, bool require_newer_revision);
    [[nodiscard]] snt::core::Expected<void> apply_delta_value(const GameInventoryDelta& delta);

    std::string local_account_id_;
    std::optional<GameInventorySnapshot> snapshot_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
