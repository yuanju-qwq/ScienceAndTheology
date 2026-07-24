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
#include "game/resources/resource_runtime_index.h"

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
    // Serializable/content-facing player stack. This type is limited to
    // persistence, replication, UI, and command boundaries; authoritative
    // ECS inventory storage uses GamePlayerRuntimeItemStack below.
    ResourceContentStack resource;
    // Empty means a normal content-defined stack. Non-empty instance data is
    // item-owned and makes the stack singular, covering durability and future
    // custom item state without carrying the legacy secondary-id API forward.
    std::string instance_data;

    [[nodiscard]] static GamePlayerItemStack item(std::string id, int64_t count,
                                                  std::string variant = {},
                                                  std::string instance_data = {}) {
        return {
            .resource = ResourceContentStack::item(
                std::move(id), count, std::move(variant)),
            .instance_data = std::move(instance_data),
        };
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return resource.key.type.empty() && resource.key.id.empty() &&
               resource.key.variant.empty() && resource.amount == 0 &&
               instance_data.empty();
    }
    [[nodiscard]] bool is_valid_item() const noexcept {
        return resource.is_valid() && resource.is_item() &&
               (instance_data.empty() || resource.amount == 1);
    }
    void clear() noexcept {
        resource = {};
        instance_data.clear();
    }

    friend bool operator==(const GamePlayerItemStack&,
                           const GamePlayerItemStack&) = default;
};

// Minecraft-style fixed inventory slots. Empty slots remain in the vector so
// hotbar selection and item placement have stable indices outside the UI.
struct GamePlayerInventory {
    // Serializable/content-facing fixed inventory. It deliberately never
    // carries ResourceKey numeric IDs or a runtime snapshot.
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

// Authoritative hot-path player inventory values. A live server component
// owns only these compact keys and keeps the immutable snapshot that issued
// them. No content strings enter stack comparisons, merges, removals, or
// slot-transfer commits; conversion happens at the server boundary.
struct GamePlayerRuntimeItemStack {
    ResourceStack resource;
    // Mutable per-item data remains outside ResourceKey. The current player
    // persistence codec owns this opaque representation until the dedicated
    // ItemInstance codec replaces it.
    std::string instance_data;

    [[nodiscard]] bool is_empty() const noexcept {
        return resource.is_absent() && instance_data.empty();
    }
    [[nodiscard]] bool is_valid_item() const noexcept {
        return resource.is_valid() &&
               (instance_data.empty() || resource.amount == 1);
    }
    void clear() noexcept {
        resource = {};
        instance_data.clear();
    }

    friend bool operator==(const GamePlayerRuntimeItemStack&,
                           const GamePlayerRuntimeItemStack&) = default;
};

// Minecraft-style fixed inventory slots bound to one immutable resource
// snapshot. The snapshot keeps all ResourceKey numeric IDs stable for the
// entire active interval between content reloads.
struct GamePlayerRuntimeInventory {
    ResourceRuntimeIndex::Snapshot resource_runtime_index;
    std::vector<GamePlayerRuntimeItemStack> slots;
    uint32_t max_slots = 36;
    int32_t max_stack_size = 64;
};

// Equipment uses the same runtime snapshot as its owning player inventory.
// It is intentionally a separate component so player appearance and combat
// can retain their fixed six-slot layout without a special key representation.
struct GamePlayerRuntimeEquipment {
    std::array<GamePlayerRuntimeItemStack, kGamePlayerEquipmentSlotCount> slots;
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

// A stable-slot transfer is the authority boundary for retained inventory UI.
// Expected stack values make the operation conditional: a delayed client drag
// cannot silently move a different item after the authoritative inventory has
// changed. A full source may swap, while a partial normal stack may move or
// merge into an empty/matching target.
struct GamePlayerInventorySlotTransfer {
    uint32_t source_slot = 0;
    uint32_t target_slot = 0;
    int32_t count = 0;
    GamePlayerItemStack expected_source;
    GamePlayerItemStack expected_target;
};

// A cross-container service uses this conditional replacement after it has
// prepared its own machine/container candidate. Expected values prevent a
// stale UI action from replacing a newer authoritative player slot.
struct GamePlayerInventorySlotMutation {
    uint32_t slot = 0;
    GamePlayerItemStack expected;
    GamePlayerItemStack replacement;
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
    // A short-lived durability receipt for an in-flight ground-loot pickup.
    // It is never replicated or interpreted as gameplay inventory; the
    // server-owned pickup journal consumes it during crash recovery before a
    // player can reconnect. Keeping the receipt in the player payload makes
    // the player/world cross-file commit observable without persisting a
    // process-local inventory handle.
    std::vector<uint64_t> ground_loot_claim_receipts;
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
