// Game-owned authoritative player value contracts.
//
// These values deliberately describe the current server authority boundary
// without importing legacy Godot inventory/equipment objects. The dedicated
// server owns their lifecycle; future persistence, player commands, AOI, and
// machine interactions exchange only these value types.

#pragma once

#include "core/expected.h"
#include "ecs/core_components.h"
#include "game/player/player_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

// A block-grid position in one game dimension. The generic Position component
// owns coordinates while this value adds game-owned dimension identity.
struct GamePlayerWorldPosition {
    std::string dimension_id;
    snt::ecs::Position position;
};

struct GamePlayerItemStack {
    std::string item_id;
    int32_t count = 0;
    // Empty means a normal content-defined stack. Non-empty instance data is
    // item-owned and makes the stack singular, covering durability and future
    // custom item state without carrying the legacy secondary-id API forward.
    std::string instance_data;

    friend bool operator==(const GamePlayerItemStack&,
                           const GamePlayerItemStack&) = default;
};

// Minecraft-style fixed inventory slots. Empty slots remain in the vector so
// hotbar selection and item placement have stable indices outside the UI.
struct GamePlayerInventory {
    std::vector<GamePlayerItemStack> slots;
    uint32_t max_slots = 36;
    int32_t max_stack_size = 64;

    friend bool operator==(const GamePlayerInventory&,
                           const GamePlayerInventory&) = default;
};

enum class GamePlayerEquipmentSlot : uint8_t {
    kMainHand = 0,
    kOffHand,
    kHead,
    kChest,
    kLegs,
    kFeet,
    kCount,
};

inline constexpr size_t kGamePlayerEquipmentSlotCount =
    static_cast<size_t>(GamePlayerEquipmentSlot::kCount);

// Equipment deliberately follows the current Minecraft-style six slots. It
// replaces the old Godot-only ARM slot rather than preserving it as legacy API.
struct GamePlayerEquipment {
    std::array<GamePlayerItemStack, kGamePlayerEquipmentSlotCount> slots;

    friend bool operator==(const GamePlayerEquipment&,
                           const GamePlayerEquipment&) = default;
};

// Tool tags are resolved by trusted gameplay code from the equipped item;
// clients never submit this value as part of an interaction command.
struct GamePlayerToolState {
    std::vector<std::string> held_tool_tags;

    friend bool operator==(const GamePlayerToolState&,
                           const GamePlayerToolState&) = default;
};

// The source-law subsystem owns interpretation of this payload. Keeping a
// schema discriminator here lets player persistence preserve organs before a
// concrete organ simulation exists, without exposing old source-law classes.
struct GamePlayerOrganState {
    std::string schema_id;
    uint16_t schema_version = 0;
    std::vector<std::byte> payload;

    friend bool operator==(const GamePlayerOrganState&,
                           const GamePlayerOrganState&) = default;
};

class IGamePlayerOrganStateCodec {
public:
    virtual ~IGamePlayerOrganStateCodec() = default;

    virtual snt::core::Expected<void> validate_organ_state(
        const GamePlayerOrganState& state) const = 0;
};

// A single inventory change is applied atomically: all removals must be
// available and all additions must fit before the live inventory is replaced.
// It is the future boundary for machine input/output and reward transactions.
struct GamePlayerInventoryTransaction {
    std::vector<GamePlayerItemStack> removals;
    std::vector<GamePlayerItemStack> additions;
};

// ECS components attached only to server-owned player entities. Peer ids stay
// in the dedicated-server service so gameplay ECS data remains transport-free.
struct GamePlayerIdentityComponent {
    PlayerIdentityProvider provider = PlayerIdentityProvider::kLocalName;
    std::string account_id;
    std::string display_name;
};

struct GamePlayerDimensionComponent {
    std::string dimension_id;
};

struct GamePlayerRespawnPointComponent {
    std::optional<GamePlayerWorldPosition> position;
};

// Value reserved for the later current-format player save. It intentionally
// excludes an entt handle, peer id, script object, and client-only UI state.
struct GamePlayerPersistentState {
    GamePlayerWorldPosition position;
    std::optional<GamePlayerWorldPosition> respawn_point;
    GamePlayerInventory inventory;
    GamePlayerEquipment equipment;
    GamePlayerOrganState organs;
};

// Persistence is declared before gameplay starts depending on player state,
// but no old player file format is read or adapted. A future implementation
// owns a new current-format file below the game universe save root.
class IGamePlayerStatePersistence {
public:
    virtual ~IGamePlayerStatePersistence() = default;

    virtual snt::core::Expected<std::optional<GamePlayerPersistentState>> load_player_state(
        std::string_view account_id) = 0;
    virtual snt::core::Expected<void> save_player_state(
        std::string_view account_id, const GamePlayerPersistentState& state) = 0;
};

}  // namespace snt::game
