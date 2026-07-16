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
#include <string>
#include <string_view>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::game::replication {

struct GameServerPlayerStateConfig {
    GamePlayerWorldPosition spawn{
        .dimension_id = "overworld",
        .position = {.x = 4, .y = 6, .z = 8},
    };
    uint32_t inventory_slots = 36;
    int32_t inventory_max_stack_size = 64;
    int32_t interaction_reach_blocks = 5;
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

class GameServerPlayerState final {
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
    [[nodiscard]] snt::core::Expected<std::vector<std::string>> held_tool_tags_for_peer(
        const GameAuthenticatedPeer& peer) const;

    // These mutation APIs receive an already authenticated peer rather than a
    // client payload. Command handlers must derive their values from server
    // simulation state before calling them.
    [[nodiscard]] snt::core::Expected<void> set_authoritative_position(
        const GameAuthenticatedPeer& peer, GamePlayerWorldPosition position);
    [[nodiscard]] snt::core::Expected<void> set_respawn_point(
        const GameAuthenticatedPeer& peer,
        std::optional<GamePlayerWorldPosition> respawn_point);
    [[nodiscard]] snt::core::Expected<bool> is_target_reachable(
        const GameAuthenticatedPeer& peer,
        const GamePlayerWorldPosition& target) const;
    [[nodiscard]] snt::core::Expected<void> apply_inventory_transaction(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventoryTransaction& transaction);
    // Runs the same all-or-nothing inventory validation without mutation.
    // Grave collection uses it before it erases durable world contents.
    [[nodiscard]] snt::core::Expected<bool> can_apply_inventory_transaction(
        const GameAuthenticatedPeer& peer,
        const GamePlayerInventoryTransaction& transaction) const;
    [[nodiscard]] snt::core::Expected<void> replace_trusted_held_tool_tags(
        const GameAuthenticatedPeer& peer, std::vector<std::string> tags);

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

    GameServerPlayerState(snt::ecs::World& world, GameServerPlayerStateConfig config);

    [[nodiscard]] snt::core::Expected<void> validate_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<void> validate_position(
        const GamePlayerWorldPosition& position) const;
    [[nodiscard]] snt::core::Expected<void> validate_inventory(
        const GamePlayerInventory& inventory) const;
    [[nodiscard]] snt::core::Expected<void> validate_equipment(
        const GamePlayerEquipment& equipment) const;
    [[nodiscard]] snt::core::Expected<void> validate_organ_state(
        const GamePlayerOrganState& organs) const;
    [[nodiscard]] snt::core::Expected<void> validate_persistent_state(
        const GamePlayerPersistentState& state) const;
    [[nodiscard]] snt::core::Expected<void> validate_inventory_transaction(
        const GamePlayerInventoryTransaction& transaction) const;
    [[nodiscard]] snt::core::Expected<void> validate_tool_tags(
        const std::vector<std::string>& tags) const;
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

    static bool is_empty_stack(const GamePlayerItemStack& stack) noexcept;
    static void clear_stack(GamePlayerItemStack& stack) noexcept;
    static bool stacks_can_merge(const GamePlayerItemStack& left,
                                 const GamePlayerItemStack& right) noexcept;
    static bool remove_items(GamePlayerInventory& inventory,
                             const GamePlayerItemStack& stack) noexcept;
    static bool add_items(GamePlayerInventory& inventory,
                          const GamePlayerItemStack& stack) noexcept;

    snt::ecs::World* world_ = nullptr;
    GameServerPlayerStateConfig config_;
    std::map<std::string, PlayerRecord, std::less<>> players_;
    std::map<snt::network::PeerId, std::string> active_peers_;
    bool stopped_ = false;
};

}  // namespace snt::game::replication
