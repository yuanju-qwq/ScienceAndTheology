// Dedicated-server authoritative player entity service.
//
// The service maps authenticated accounts to live ECS actors, preserves one
// actor through an in-process account takeover, and exposes only server-
// trusted position, inventory, and tool-tag operations. A normal disconnect
// destroys the actor; GameServerPlayerLifecycle owns persistence and restores
// a fresh actor on the next login. It is main-thread-only and intentionally
// does not parse client commands.

#pragma once

#include "core/expected.h"
#include "ecs/entt_config.h"
#include "ecs/entity_guid.h"
#include "game/network/game_replication_services.h"
#include "game/player/player_state.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::game::replication {

struct GameServerPlayerStateConfig {
    // Required immutable mapping for every live inventory/equipment stack.
    // Content reloads use rebind_resource_snapshot() rather than allowing
    // numeric ResourceKey values from two generations to coexist.
    ResourceRuntimeIndex::Snapshot resource_runtime_index;
    // Optional game-owned schema validator. The server keeps the generic
    // player envelope, while a gameplay module owns its opaque payload.
    const IGamePlayerOrganStateCodec* organ_state_codec = nullptr;
    GamePlayerWorldPosition spawn{
        .dimension_id = "overworld",
        .position = {.x = 4, .y = 6, .z = 8},
    };
    uint32_t inventory_slots = 36;
    int32_t inventory_max_stack_size = 64;
    int32_t interaction_reach_blocks = 5;
    // Combat health is an online-only actor value. The server combat service
    // resets it after a committed death transaction rather than serializing
    // a legacy vitals payload into SNTP.
    float combat_max_health = 20.0f;
};

// A copied, transport-safe view for future AOI and player replication code.
struct GameServerPlayerSnapshot {
    PlayerIdentityProvider identity_provider = PlayerIdentityProvider::kLocalName;
    std::string account_id;
    std::string display_name;
    snt::ecs::EntityGuid entity_guid;
    GamePlayerWorldPosition position;
    GamePlayerEquipment equipment;
    snt::network::PeerId peer = snt::network::kInvalidPeerId;
};

class GameServerPlayerState final : public IResourceRuntimeSnapshotParticipant {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerState>> create(
        snt::ecs::World& world, GameServerPlayerStateConfig config = {});

    GameServerPlayerState(const GameServerPlayerState&) = delete;
    GameServerPlayerState& operator=(const GameServerPlayerState&) = delete;

    // Lifecycle calls are ordered by GameServerPlayerLifecycle after account
    // identity has been authenticated and before gameplay commands can run.
    [[nodiscard]] snt::core::Expected<void> on_peer_authenticated(
        const GameAuthenticatedPeer& peer, const GamePlayerPersistentState& state);
    [[nodiscard]] snt::core::Expected<void> on_peer_replaced(
        const GameAuthenticatedPeer& previous_peer,
        const GameAuthenticatedPeer& replacement_peer);
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept;

    [[nodiscard]] snt::core::Expected<GameServerPlayerSnapshot> snapshot_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<std::vector<GameServerPlayerSnapshot>>
    active_player_snapshots() const;
    [[nodiscard]] snt::core::Expected<GamePlayerInventory> inventory_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<GamePlayerEquipment> equipment_for_peer(
        const GameAuthenticatedPeer& peer) const;
    // Source-law owns the payload schema, while this service keeps the live
    // ECS component and persistence envelope authoritative. These narrow
    // accessors prevent a source-law runtime from reaching into World/ECS.
    [[nodiscard]] snt::core::Expected<GamePlayerOrganState> organ_state_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<void> set_authoritative_organ_state(
        const GameAuthenticatedPeer& peer, GamePlayerOrganState organs);
    [[nodiscard]] snt::core::Expected<GamePlayerCombatState> combat_state_for_peer(
        const GameAuthenticatedPeer& peer) const;
    // Only the server combat service calls these mutations after it has
    // accepted a trusted server-side damage event. They intentionally are not
    // reachable from client command payloads.
    [[nodiscard]] snt::core::Expected<void> apply_nonlethal_combat_damage(
        const GameAuthenticatedPeer& peer, float damage);
    [[nodiscard]] snt::core::Expected<void> restore_full_combat_health(
        const GameAuthenticatedPeer& peer);
    // Rebinds every active player's compact inventory/equipment keys through
    // the stable content-key boundary. The operation is all-or-nothing: an
    // unresolved content definition leaves every active component and the
    // currently published player snapshot unchanged.
    [[nodiscard]] snt::core::Expected<void> rebind_resource_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot);
    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;
    // Gameplay action services derive behavior from this server-owned main
    // hand value through the content catalog; clients never provide a tool
    // identity as an authority input.
    [[nodiscard]] snt::core::Expected<std::string> main_hand_item_id_for_peer(
        const GameAuthenticatedPeer& peer) const;

    // Server-composition services receive stable account ids from committed
    // gameplay events. This resolves that id back to the currently active,
    // authenticated session without accepting any client supplied identity.
    [[nodiscard]] snt::core::Expected<GameAuthenticatedPeer> active_peer_for_account(
        std::string_view account_id) const;

    // These mutation APIs receive an already authenticated peer rather than a
    // client payload. Command handlers must derive their values from server
    // simulation state before calling them.
    [[nodiscard]] snt::core::Expected<void> set_authoritative_position(
        const GameAuthenticatedPeer& peer, GamePlayerWorldPosition position);
    [[nodiscard]] snt::core::Expected<void> set_respawn_point(
        const GameAuthenticatedPeer& peer,
        std::optional<GamePlayerWorldPosition> respawn_point);
    // Authoritative equipment changes cross the content boundary here. The
    // ECS component remains compact and bound to the player's inventory
    // snapshot, so callers cannot insert a numeric key from another reload.
    [[nodiscard]] snt::core::Expected<void> set_authoritative_equipment(
        const GameAuthenticatedPeer& peer, GamePlayerEquipment equipment);
    [[nodiscard]] snt::core::Expected<bool> is_target_reachable(
        const GameAuthenticatedPeer& peer,
        const GamePlayerWorldPosition& target) const;
    // Dynamic targets such as native creatures are not block-aligned. Keep
    // their reach check inside player authority so gameplay services never
    // duplicate the configured range or accept a client-derived position.
    [[nodiscard]] snt::core::Expected<bool> is_point_reachable(
        const GameAuthenticatedPeer& peer, std::string_view dimension_id,
        float position_x, float position_y, float position_z) const;
    [[nodiscard]] snt::core::Expected<void> apply_inventory_transaction(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventoryTransaction& transaction);
    // Runs the same all-or-nothing inventory validation without mutation.
    // Grave collection uses it before it erases durable world contents.
    [[nodiscard]] snt::core::Expected<bool> can_apply_inventory_transaction(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventoryTransaction& transaction) const;
    // Applies a conditional stable-slot transfer and returns the committed
    // inventory snapshot for a client confirmation adapter. The server alone
    // chooses whether a delayed UI request still matches its expected slots.
    [[nodiscard]] snt::core::Expected<GamePlayerInventory> apply_inventory_slot_transfer(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventorySlotTransfer& transfer);
    [[nodiscard]] snt::core::Expected<bool> can_apply_inventory_slot_transfer(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventorySlotTransfer& transfer) const;
    // Main-thread cross-container services use this value-only conditional
    // boundary after validating their own container candidate. All listed
    // player slots are replaced atomically or none are changed.
    [[nodiscard]] snt::core::Expected<void> apply_inventory_slot_mutations(
        const GameAuthenticatedPeer& peer,
        std::span<const GamePlayerInventorySlotMutation> mutations);
    [[nodiscard]] snt::core::Expected<bool> can_apply_inventory_slot_mutations(
        const GameAuthenticatedPeer& peer,
        std::span<const GamePlayerInventorySlotMutation> mutations) const;

    // The lifecycle uses these value boundaries for first join, disconnect
    // save, and controlled shutdown. No entt handle or transport id leaks
    // into the persistent representation.
    [[nodiscard]] GamePlayerPersistentState default_persistent_state() const;
    [[nodiscard]] snt::core::Expected<GamePlayerPersistentState> capture_persistent_state(
        const GameAuthenticatedPeer& peer) const;

    [[nodiscard]] size_t active_player_count() const noexcept { return active_peers_.size(); }

    // Explicit shutdown destroys only actor entities created by this service.
    // The caller must invoke it before destroying the referenced ECS World.
    void shutdown() noexcept;

private:
    struct PlayerRecord {
        snt::ecs::EntityGuid entity_guid;
        snt::network::PeerId peer = snt::network::kInvalidPeerId;
    };
    struct RuntimeInventoryTransaction;
    struct RuntimeInventorySlotTransfer;
    struct RuntimeInventorySlotMutation;
    struct PendingResourceSnapshotRebind {
        struct Player {
            entt::entity entity = entt::null;
            GamePlayerRuntimeInventory inventory;
            GamePlayerRuntimeEquipment equipment;
        };

        ResourceRuntimeIndex::Snapshot next_snapshot;
        std::vector<Player> players;
    };

    GameServerPlayerState(snt::ecs::World& world, GameServerPlayerStateConfig config);

    [[nodiscard]] snt::core::Expected<void> validate_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<void> validate_position(
        const GamePlayerWorldPosition& position) const;
    [[nodiscard]] snt::core::Expected<void> validate_content_inventory(
        const GamePlayerInventory& inventory) const;
    [[nodiscard]] snt::core::Expected<void> validate_content_equipment(
        const GamePlayerEquipment& equipment) const;
    [[nodiscard]] snt::core::Expected<void> validate_runtime_inventory(
        const GamePlayerRuntimeInventory& inventory) const;
    [[nodiscard]] snt::core::Expected<void> validate_runtime_equipment(
        const GamePlayerRuntimeEquipment& equipment) const;
    [[nodiscard]] snt::core::Expected<void> validate_organ_state(
        const GamePlayerOrganState& organs) const;
    [[nodiscard]] snt::core::Expected<void> validate_persistent_state(
        const GamePlayerPersistentState& state) const;
    [[nodiscard]] snt::core::Expected<void> validate_inventory_transaction(
        const GamePlayerInventoryTransaction& transaction) const;
    [[nodiscard]] snt::core::Expected<void> validate_inventory_slot_transfer(
        const GamePlayerInventorySlotTransfer& transfer) const;
    [[nodiscard]] snt::core::Expected<void> validate_inventory_slot_mutations(
        std::span<const GamePlayerInventorySlotMutation> mutations) const;
    [[nodiscard]] snt::core::Expected<GamePlayerRuntimeInventory> resolve_runtime_inventory(
        const GamePlayerInventory& inventory,
        const ResourceRuntimeIndex::Snapshot& resource_index) const;
    [[nodiscard]] snt::core::Expected<GamePlayerRuntimeEquipment> resolve_runtime_equipment(
        const GamePlayerEquipment& equipment,
        const ResourceRuntimeIndex::Snapshot& resource_index) const;
    [[nodiscard]] snt::core::Expected<GamePlayerInventory> resolve_content_inventory(
        const GamePlayerRuntimeInventory& inventory) const;
    [[nodiscard]] snt::core::Expected<GamePlayerEquipment> resolve_content_equipment(
        const GamePlayerRuntimeEquipment& equipment,
        const ResourceRuntimeIndex::Snapshot& resource_index) const;
    [[nodiscard]] snt::core::Expected<RuntimeInventoryTransaction>
    resolve_runtime_inventory_transaction(
        const GamePlayerInventoryTransaction& transaction) const;
    [[nodiscard]] snt::core::Expected<RuntimeInventorySlotTransfer>
    resolve_runtime_inventory_slot_transfer(
        const GamePlayerInventorySlotTransfer& transfer) const;
    [[nodiscard]] snt::core::Expected<std::vector<RuntimeInventorySlotMutation>>
    resolve_runtime_inventory_slot_mutations(
        std::span<const GamePlayerInventorySlotMutation> mutations) const;
    [[nodiscard]] snt::core::Expected<PlayerRecord*> find_active_record(
        const GameAuthenticatedPeer& peer);
    [[nodiscard]] snt::core::Expected<const PlayerRecord*> find_active_record(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<entt::entity> entity_for_record(
        const PlayerRecord& record) const;
    [[nodiscard]] snt::core::Expected<GameServerPlayerSnapshot> snapshot_for_record(
        const PlayerRecord& record) const;
    [[nodiscard]] snt::core::Expected<snt::ecs::EntityGuid> create_player_entity(
        const GameAuthenticatedPeer& peer, const GamePlayerPersistentState& state);

    static bool is_empty_stack(const GamePlayerRuntimeItemStack& stack) noexcept;
    static void clear_stack(GamePlayerRuntimeItemStack& stack) noexcept;
    static bool stacks_can_merge(const GamePlayerRuntimeItemStack& left,
                                 const GamePlayerRuntimeItemStack& right) noexcept;
    static bool remove_items(GamePlayerRuntimeInventory& inventory,
                             const GamePlayerRuntimeItemStack& stack) noexcept;
    static bool add_items(GamePlayerRuntimeInventory& inventory,
                          const GamePlayerRuntimeItemStack& stack) noexcept;
    static bool apply_slot_transfer(GamePlayerRuntimeInventory& inventory,
                                    const RuntimeInventorySlotTransfer& transfer) noexcept;

    snt::ecs::World* world_ = nullptr;
    GameServerPlayerStateConfig config_;
    ResourceRuntimeIndex::Snapshot resource_runtime_index_;
    std::optional<PendingResourceSnapshotRebind> pending_resource_snapshot_rebind_;
    std::map<std::string, PlayerRecord, std::less<>> players_;
    std::map<snt::network::PeerId, std::string> active_peers_;
    bool stopped_ = false;
};

}  // namespace snt::game::replication
