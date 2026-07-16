// Game-owned player death, grave, and respawn contracts.
//
// A concrete service must atomically create an indestructible grave before it
// clears non-equipment inventory, then resolve a safe bed-or-world spawn. The
// owning world/block-entity module supplies the implementation; this header
// deliberately contains no client, transport, or legacy Godot API.

#pragma once

#include "core/expected.h"
#include "game/player/player_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct GamePlayerGraveId {
    uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept { return value != 0; }
};

struct GamePlayerGraveCreateRequest {
    std::string owner_account_id;
    GamePlayerWorldPosition death_position;
    uint64_t death_tick = 0;
    // Equipment is intentionally absent: it remains equipped through death.
    GamePlayerInventory contents;
};

// A grave read is always authorized against a stable account id. Server-side
// administration may opt in explicitly; client payloads never choose this.
struct GamePlayerGraveAccess {
    std::string_view requester_account_id;
    bool is_administrator = false;
};

// Contents are a copied value boundary for a grave UI or an all-or-nothing
// inventory transfer. They never expose a world/block-entity implementation.
struct GamePlayerGraveContents {
    GamePlayerGraveId id;
    std::string owner_account_id;
    GamePlayerWorldPosition position;
    uint64_t death_tick = 0;
    std::vector<GamePlayerItemStack> items;
};

struct GamePlayerDeathResult {
    std::optional<GamePlayerGraveId> grave_id;
    GamePlayerWorldPosition respawn_position;
};

enum class GamePlayerGraveClaimStatus : uint8_t {
    kCollected = 0,
    kInventoryFull,
};

struct GamePlayerGraveClaimResult {
    GamePlayerGraveClaimStatus status = GamePlayerGraveClaimStatus::kCollected;
    GamePlayerGraveContents contents;
};

// The returned grave must be backed by an indestructible world entity/block
// and own its contents transactionally. The store provides explicit read and
// erase operations so the server can preflight an inventory transfer before
// committing a claim; it must never delete a grave on a failed preflight.
class IGamePlayerGraveStore {
public:
    virtual ~IGamePlayerGraveStore() = default;

    virtual snt::core::Expected<GamePlayerGraveId> create_indestructible_grave(
        const GamePlayerGraveCreateRequest& request) = 0;
    virtual snt::core::Expected<GamePlayerGraveContents> read_grave(
        GamePlayerGraveId id, const GamePlayerGraveAccess& access) const = 0;
    virtual snt::core::Expected<void> erase_grave(
        GamePlayerGraveId id, const GamePlayerGraveAccess& access) = 0;
};

// Bed placement belongs to a later block-edit transaction, while respawn
// lookup only needs a read-only answer. Keeping this contract narrow prevents
// player persistence from owning world block state.
class IGamePlayerBedLocator {
public:
    virtual ~IGamePlayerBedLocator() = default;

    virtual snt::core::Expected<bool> has_bed_at(
        const GamePlayerWorldPosition& position) const = 0;
};

// Validates a stored bed point and falls back to a world spawn/safe-cell
// policy. It keeps spatial world queries outside player persistence.
class IGamePlayerRespawnResolver {
public:
    virtual ~IGamePlayerRespawnResolver() = default;

    virtual snt::core::Expected<GamePlayerWorldPosition> resolve_respawn(
        std::string_view account_id,
        const std::optional<GamePlayerWorldPosition>& saved_respawn_point) = 0;
};

}  // namespace snt::game
