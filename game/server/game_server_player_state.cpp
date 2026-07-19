// Dedicated-server authoritative player entity service implementation.

#define SNT_LOG_CHANNEL "game.server_player_state"
#include "game/server/game_server_player_state.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxDimensionIdBytes = 128;
constexpr size_t kMaxItemIdBytes = 256;
constexpr size_t kMaxItemInstanceDataBytes = 16u * 1024u;
constexpr size_t kMaxToolTagBytes = 128;
constexpr size_t kMaxToolTags = 64;
constexpr size_t kMaxOrganSchemaIdBytes = 128;
constexpr size_t kMaxOrganPayloadBytes = 64u * 1024u;
constexpr uint32_t kMaxInventorySlots = 256;
constexpr int32_t kMaxInventoryStackSize = 65536;
constexpr size_t kMaxInventoryTransactionEntries = 128;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_empty_stack_value(const GamePlayerItemStack& stack) noexcept {
    return stack.item_id.empty() && stack.count == 0 && stack.instance_data.empty();
}

[[nodiscard]] bool has_valid_nonempty_stack_shape(const GamePlayerItemStack& stack) noexcept {
    if (stack.item_id.empty() || stack.item_id.size() > kMaxItemIdBytes || stack.count <= 0 ||
        stack.instance_data.size() > kMaxItemInstanceDataBytes) {
        return false;
    }
    return stack.instance_data.empty() || stack.count == 1;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerState>> GameServerPlayerState::create(
    snt::ecs::World& world, GameServerPlayerStateConfig config) {
    if (config.spawn.dimension_id.empty() ||
        config.spawn.dimension_id.size() > kMaxDimensionIdBytes) {
        return invalid_argument("Dedicated server player spawn dimension id is invalid");
    }
    if (config.inventory_slots == 0 || config.inventory_slots > kMaxInventorySlots) {
        return invalid_argument("Dedicated server player inventory slot count is invalid");
    }
    if (config.inventory_max_stack_size <= 0 ||
        config.inventory_max_stack_size > kMaxInventoryStackSize) {
        return invalid_argument("Dedicated server player inventory stack size is invalid");
    }
    if (config.interaction_reach_blocks <= 0 || config.interaction_reach_blocks > 256) {
        return invalid_argument("Dedicated server player interaction reach is invalid");
    }
    return std::unique_ptr<GameServerPlayerState>(
        new GameServerPlayerState(world, std::move(config)));
}

GameServerPlayerState::GameServerPlayerState(snt::ecs::World& world,
                                             GameServerPlayerStateConfig config)
    : world_(&world), config_(std::move(config)) {}

GamePlayerPersistentState GameServerPlayerState::default_persistent_state() const {
    return {
        .position = config_.spawn,
        .respawn_point = std::nullopt,
        .inventory = {
            .slots = std::vector<GamePlayerItemStack>(config_.inventory_slots),
            .max_slots = config_.inventory_slots,
            .max_stack_size = config_.inventory_max_stack_size,
        },
        .equipment = {},
        .organs = {},
    };
}

snt::core::Expected<void> GameServerPlayerState::on_peer_authenticated(
    const GameAuthenticatedPeer& peer, const GamePlayerPersistentState& state) {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (auto result = validate_peer(peer); !result) return result.error();
    if (auto result = validate_persistent_state(state); !result) return result.error();
    if (active_peers_.contains(peer.peer)) {
        return invalid_state("Dedicated server player state already owns this transport peer");
    }
    if (players_.contains(peer.identity.account_id)) {
        return invalid_state("Dedicated server player account is already active");
    }

    auto guid = create_player_entity(peer, state);
    if (!guid) return guid.error();
    const auto [created, inserted] = players_.emplace(
        peer.identity.account_id, PlayerRecord{.entity_guid = *guid, .peer = peer.peer});
    static_cast<void>(created);
    if (!inserted) {
        const entt::entity entity = world_->find_entity_by_guid(*guid);
        if (entity != entt::null) world_->destroy_entity(entity);
        return invalid_state("Dedicated server player account was created twice");
    }
    active_peers_.emplace(peer.peer, peer.identity.account_id);
    SNT_LOG_INFO("Created authoritative player entity guid=%llu for account '%s'",
                 static_cast<unsigned long long>(guid->value), peer.identity.account_id.c_str());
    return {};
}

snt::core::Expected<void> GameServerPlayerState::on_peer_replaced(
    const GameAuthenticatedPeer& previous_peer,
    const GameAuthenticatedPeer& replacement_peer) {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (auto result = validate_peer(previous_peer); !result) return result.error();
    if (auto result = validate_peer(replacement_peer); !result) return result.error();
    if (previous_peer.peer == replacement_peer.peer) {
        return invalid_argument("Authoritative player takeover requires distinct transport peers");
    }
    if (previous_peer.identity.account_id != replacement_peer.identity.account_id) {
        return invalid_argument("Authoritative player takeover requires the same stable account id");
    }
    if (active_peers_.contains(replacement_peer.peer)) {
        return invalid_state("Authoritative player replacement peer is already active");
    }

    const auto active = active_peers_.find(previous_peer.peer);
    const auto account = players_.find(previous_peer.identity.account_id);
    if (active == active_peers_.end() || account == players_.end() ||
        active->second != previous_peer.identity.account_id ||
        account->second.peer != previous_peer.peer) {
        return invalid_state("Authoritative player takeover has no matching active player");
    }
    auto entity = entity_for_record(account->second);
    if (!entity) return entity.error();

    auto& identity = world_->get_component<GamePlayerIdentityComponent>(*entity);
    identity.provider = replacement_peer.identity.provider;
    identity.display_name = replacement_peer.identity.display_name;
    active_peers_.erase(active);
    active_peers_.emplace(replacement_peer.peer, replacement_peer.identity.account_id);
    account->second.peer = replacement_peer.peer;
    SNT_LOG_INFO("Transferred authoritative player entity guid=%llu from peer %llu to peer %llu",
                 static_cast<unsigned long long>(account->second.entity_guid.value),
                 static_cast<unsigned long long>(previous_peer.peer),
                 static_cast<unsigned long long>(replacement_peer.peer));
    return {};
}

void GameServerPlayerState::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                  std::string_view reason) noexcept {
    if (stopped_ || peer.peer == snt::network::kInvalidPeerId) return;
    const auto active = active_peers_.find(peer.peer);
    if (active == active_peers_.end() || active->second != peer.identity.account_id) return;
    const auto account = players_.find(active->second);
    if (account != players_.end() && account->second.peer == peer.peer) {
        const entt::entity entity = world_->find_entity_by_guid(account->second.entity_guid);
        if (entity != entt::null) world_->destroy_entity(entity);
        SNT_LOG_INFO("Destroyed disconnected player entity guid=%llu for account '%s' (%.*s)",
                     static_cast<unsigned long long>(account->second.entity_guid.value),
                     account->first.c_str(), static_cast<int>(reason.size()), reason.data());
        players_.erase(account);
    }
    active_peers_.erase(active);
}

snt::core::Expected<GameServerPlayerSnapshot> GameServerPlayerState::snapshot_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    return snapshot_for_record(**record);
}

snt::core::Expected<std::vector<GameServerPlayerSnapshot>>
GameServerPlayerState::active_player_snapshots() const {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    std::vector<GameServerPlayerSnapshot> snapshots;
    snapshots.reserve(players_.size());
    for (const auto& [account_id, record] : players_) {
        static_cast<void>(account_id);
        auto snapshot = snapshot_for_record(record);
        if (!snapshot) return snapshot.error();
        snapshots.push_back(std::move(*snapshot));
    }
    return snapshots;
}

snt::core::Expected<GamePlayerInventory> GameServerPlayerState::inventory_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    return world_->get_component<GamePlayerInventory>(*entity);
}

snt::core::Expected<GamePlayerEquipment> GameServerPlayerState::equipment_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    return world_->get_component<GamePlayerEquipment>(*entity);
}

snt::core::Expected<std::vector<std::string>> GameServerPlayerState::held_tool_tags_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    return world_->get_component<GamePlayerToolState>(*entity).held_tool_tags;
}

snt::core::Expected<GameAuthenticatedPeer> GameServerPlayerState::active_peer_for_account(
    std::string_view account_id) const {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (account_id.empty()) return invalid_argument("Authoritative player account id must not be empty");
    const auto player = players_.find(account_id);
    if (player == players_.end() || player->second.peer == snt::network::kInvalidPeerId) {
        return invalid_state("Authoritative player account is not active");
    }
    const auto active = active_peers_.find(player->second.peer);
    if (active == active_peers_.end() || active->second != account_id) {
        return invalid_state("Authoritative player account has inconsistent active peer state");
    }
    auto entity = entity_for_record(player->second);
    if (!entity) return entity.error();
    const auto& identity = world_->get_component<GamePlayerIdentityComponent>(*entity);
    if (identity.account_id != account_id) {
        return invalid_state("Authoritative player entity has an inconsistent account id");
    }
    return GameAuthenticatedPeer{
        .peer = player->second.peer,
        .identity = {
            .provider = identity.provider,
            .account_id = identity.account_id,
            .display_name = identity.display_name,
        },
    };
}

snt::core::Expected<GamePlayerPersistentState> GameServerPlayerState::capture_persistent_state(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    const auto& dimension = world_->get_component<GamePlayerDimensionComponent>(*entity);
    return GamePlayerPersistentState{
        .position = {
            .dimension_id = dimension.dimension_id,
            .position = world_->get_component<snt::ecs::Position>(*entity),
        },
        .respawn_point = world_->get_component<GamePlayerRespawnPointComponent>(*entity).position,
        .inventory = world_->get_component<GamePlayerInventory>(*entity),
        .equipment = world_->get_component<GamePlayerEquipment>(*entity),
        .organs = world_->get_component<GamePlayerOrganState>(*entity),
    };
}

snt::core::Expected<void> GameServerPlayerState::set_authoritative_position(
    const GameAuthenticatedPeer& peer, GamePlayerWorldPosition position) {
    if (auto result = validate_position(position); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    world_->get_component<GamePlayerDimensionComponent>(*entity).dimension_id =
        std::move(position.dimension_id);
    world_->get_component<snt::ecs::Position>(*entity) = position.position;
    return {};
}

snt::core::Expected<void> GameServerPlayerState::set_respawn_point(
    const GameAuthenticatedPeer& peer,
    std::optional<GamePlayerWorldPosition> respawn_point) {
    if (respawn_point.has_value()) {
        if (auto result = validate_position(*respawn_point); !result) return result.error();
    }
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    world_->get_component<GamePlayerRespawnPointComponent>(*entity).position =
        std::move(respawn_point);
    return {};
}

snt::core::Expected<bool> GameServerPlayerState::is_target_reachable(
    const GameAuthenticatedPeer& peer, const GamePlayerWorldPosition& target) const {
    if (auto result = validate_position(target); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    const auto& dimension = world_->get_component<GamePlayerDimensionComponent>(*entity);
    const auto& position = world_->get_component<snt::ecs::Position>(*entity);
    if (dimension.dimension_id != target.dimension_id) return false;

    const int64_t reach = config_.interaction_reach_blocks;
    const int64_t dx = static_cast<int64_t>(target.position.x) - position.x;
    const int64_t dy = static_cast<int64_t>(target.position.y) - position.y;
    const int64_t dz = static_cast<int64_t>(target.position.z) - position.z;
    if (dx < -reach || dx > reach || dy < -reach || dy > reach || dz < -reach || dz > reach) {
        return false;
    }
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

snt::core::Expected<void> GameServerPlayerState::apply_inventory_transaction(
    const GameAuthenticatedPeer& peer, const GamePlayerInventoryTransaction& transaction) {
    auto applicable = can_apply_inventory_transaction(peer, transaction);
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return invalid_state("Authoritative player inventory cannot apply the requested transaction");
    }

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    for (const GamePlayerItemStack& removal : transaction.removals) {
        const bool removed = remove_items(candidate, removal);
        if (!removed) {
            return invalid_state("Authoritative player inventory changed during transaction apply");
        }
    }
    for (const GamePlayerItemStack& addition : transaction.additions) {
        const bool added = add_items(candidate, addition);
        if (!added) {
            return invalid_state("Authoritative player inventory changed during transaction apply");
        }
    }
    world_->get_component<GamePlayerInventory>(*entity) = std::move(candidate);
    return {};
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_transaction(
    const GameAuthenticatedPeer& peer, const GamePlayerInventoryTransaction& transaction) const {
    if (auto result = validate_inventory_transaction(transaction); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    if (auto result = validate_inventory(candidate); !result) return result.error();
    for (const GamePlayerItemStack& removal : transaction.removals) {
        if (!remove_items(candidate, removal)) {
            return false;
        }
    }
    for (const GamePlayerItemStack& addition : transaction.additions) {
        if (!add_items(candidate, addition)) {
            return false;
        }
    }
    return true;
}

snt::core::Expected<GamePlayerInventory> GameServerPlayerState::apply_inventory_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GamePlayerInventorySlotTransfer& transfer) {
    auto applicable = can_apply_inventory_slot_transfer(peer, transfer);
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return invalid_state("Authoritative player inventory rejected the requested slot transfer");
    }

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    if (!apply_slot_transfer(candidate, transfer)) {
        return invalid_state("Authoritative player inventory changed during slot transfer apply");
    }
    world_->get_component<GamePlayerInventory>(*entity) = candidate;
    return candidate;
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GamePlayerInventorySlotTransfer& transfer) const {
    if (auto result = validate_inventory_slot_transfer(transfer); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    if (auto result = validate_inventory(candidate); !result) return result.error();
    return apply_slot_transfer(candidate, transfer);
}

snt::core::Expected<void> GameServerPlayerState::apply_inventory_slot_mutations(
    const GameAuthenticatedPeer& peer,
    std::span<const GamePlayerInventorySlotMutation> mutations) {
    auto applicable = can_apply_inventory_slot_mutations(peer, mutations);
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return invalid_state("Authoritative player inventory rejected conditional slot mutations");
    }

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    for (const GamePlayerInventorySlotMutation& mutation : mutations) {
        if (mutation.slot >= candidate.slots.size() ||
            candidate.slots[mutation.slot] != mutation.expected) {
            return invalid_state(
                "Authoritative player inventory changed during conditional slot mutation apply");
        }
        candidate.slots[mutation.slot] = mutation.replacement;
    }
    if (auto result = validate_inventory(candidate); !result) return result.error();
    world_->get_component<GamePlayerInventory>(*entity) = std::move(candidate);
    return {};
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_slot_mutations(
    const GameAuthenticatedPeer& peer,
    std::span<const GamePlayerInventorySlotMutation> mutations) const {
    if (auto result = validate_inventory_slot_mutations(mutations); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerInventory candidate = world_->get_component<GamePlayerInventory>(*entity);
    if (auto result = validate_inventory(candidate); !result) return result.error();
    for (const GamePlayerInventorySlotMutation& mutation : mutations) {
        if (mutation.slot >= candidate.slots.size() ||
            candidate.slots[mutation.slot] != mutation.expected) {
            return false;
        }
        candidate.slots[mutation.slot] = mutation.replacement;
    }
    if (auto result = validate_inventory(candidate); !result) return result.error();
    return true;
}

snt::core::Expected<void> GameServerPlayerState::replace_trusted_held_tool_tags(
    const GameAuthenticatedPeer& peer, std::vector<std::string> tags) {
    if (auto result = validate_tool_tags(tags); !result) return result.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    world_->get_component<GamePlayerToolState>(*entity).held_tool_tags = std::move(tags);
    return {};
}

void GameServerPlayerState::shutdown() noexcept {
    if (stopped_) return;
    stopped_ = true;
    size_t destroyed = 0;
    if (world_ != nullptr) {
        for (const auto& [account_id, record] : players_) {
            static_cast<void>(account_id);
            const entt::entity entity = world_->find_entity_by_guid(record.entity_guid);
            if (entity == entt::null) continue;
            world_->destroy_entity(entity);
            ++destroyed;
        }
    }
    players_.clear();
    active_peers_.clear();
    if (destroyed != 0) {
        SNT_LOG_INFO("Destroyed %zu active authoritative player entity(s) during shutdown", destroyed);
    }
}

snt::core::Expected<void> GameServerPlayerState::validate_peer(
    const GameAuthenticatedPeer& peer) const {
    if (peer.peer == snt::network::kInvalidPeerId) {
        return invalid_argument("Authoritative player state requires an authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerState::validate_peer(identity)");
        return error;
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_position(
    const GamePlayerWorldPosition& position) const {
    if (position.dimension_id.empty() || position.dimension_id.size() > kMaxDimensionIdBytes) {
        return invalid_argument("Authoritative player position has an invalid dimension id");
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_inventory(
    const GamePlayerInventory& inventory) const {
    if (inventory.max_slots != config_.inventory_slots ||
        inventory.max_stack_size != config_.inventory_max_stack_size) {
        return invalid_state("Authoritative player inventory has incompatible capacity settings");
    }
    if (inventory.slots.size() != inventory.max_slots) {
        return invalid_state("Authoritative player inventory does not have fixed slot storage");
    }
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (is_empty_stack(stack)) continue;
        if (!has_valid_nonempty_stack_shape(stack) || stack.count > inventory.max_stack_size) {
            return invalid_state("Authoritative player inventory contains an invalid item stack");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_equipment(
    const GamePlayerEquipment& equipment) const {
    for (const GamePlayerItemStack& stack : equipment.slots) {
        if (is_empty_stack(stack)) continue;
        if (!has_valid_nonempty_stack_shape(stack) || stack.count != 1) {
            return invalid_state("Authoritative player equipment contains an invalid item stack");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_organ_state(
    const GamePlayerOrganState& organs) const {
    if (organs.schema_id.empty()) {
        if (organs.schema_version != 0 || !organs.payload.empty()) {
            return invalid_state("Authoritative player organs have an incomplete schema declaration");
        }
        return {};
    }
    if (organs.schema_id.size() > kMaxOrganSchemaIdBytes || organs.schema_version == 0 ||
        organs.payload.size() > kMaxOrganPayloadBytes) {
        return invalid_state("Authoritative player organs exceed persistence limits");
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_persistent_state(
    const GamePlayerPersistentState& state) const {
    if (auto result = validate_position(state.position); !result) return result.error();
    if (state.respawn_point.has_value()) {
        if (auto result = validate_position(*state.respawn_point); !result) return result.error();
    }
    if (auto result = validate_inventory(state.inventory); !result) return result.error();
    if (auto result = validate_equipment(state.equipment); !result) return result.error();
    return validate_organ_state(state.organs);
}

snt::core::Expected<void> GameServerPlayerState::validate_inventory_transaction(
    const GamePlayerInventoryTransaction& transaction) const {
    const size_t count = transaction.removals.size() + transaction.additions.size();
    if (count == 0 || count > kMaxInventoryTransactionEntries) {
        return invalid_argument("Authoritative player inventory transaction has an invalid operation count");
    }
    const auto validate_stack = [](const GamePlayerItemStack& stack) {
        return has_valid_nonempty_stack_shape(stack);
    };
    for (const GamePlayerItemStack& stack : transaction.removals) {
        if (!validate_stack(stack)) {
            return invalid_argument("Authoritative player inventory transaction has an invalid removal");
        }
    }
    for (const GamePlayerItemStack& stack : transaction.additions) {
        if (!validate_stack(stack)) {
            return invalid_argument("Authoritative player inventory transaction has an invalid addition");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_inventory_slot_transfer(
    const GamePlayerInventorySlotTransfer& transfer) const {
    if (transfer.source_slot == transfer.target_slot || transfer.count <= 0) {
        return invalid_argument("Authoritative player slot transfer has an invalid source, target, or count");
    }
    if (!has_valid_nonempty_stack_shape(transfer.expected_source)) {
        return invalid_argument("Authoritative player slot transfer has an invalid expected source");
    }
    if (!is_empty_stack_value(transfer.expected_target) &&
        !has_valid_nonempty_stack_shape(transfer.expected_target)) {
        return invalid_argument("Authoritative player slot transfer has an invalid expected target");
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_inventory_slot_mutations(
    std::span<const GamePlayerInventorySlotMutation> mutations) const {
    if (mutations.empty() || mutations.size() > kMaxInventoryTransactionEntries) {
        return invalid_argument("Authoritative player conditional slot mutation count is invalid");
    }
    std::vector<bool> seen(config_.inventory_slots, false);
    for (const GamePlayerInventorySlotMutation& mutation : mutations) {
        if (mutation.slot >= config_.inventory_slots || seen[mutation.slot]) {
            return invalid_argument(
                "Authoritative player conditional slot mutation has an invalid or duplicate slot");
        }
        seen[mutation.slot] = true;
        const auto valid_stack = [](const GamePlayerItemStack& stack) {
            return is_empty_stack_value(stack) || has_valid_nonempty_stack_shape(stack);
        };
        if (!valid_stack(mutation.expected) || !valid_stack(mutation.replacement)) {
            return invalid_argument(
                "Authoritative player conditional slot mutation has an invalid stack value");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_tool_tags(
    const std::vector<std::string>& tags) const {
    if (tags.size() > kMaxToolTags) {
        return invalid_argument("Authoritative player tool tag count exceeds the limit");
    }
    for (const std::string& tag : tags) {
        if (tag.empty() || tag.size() > kMaxToolTagBytes) {
            return invalid_argument("Authoritative player tool tag is invalid");
        }
    }
    return {};
}

snt::core::Expected<GameServerPlayerState::PlayerRecord*>
GameServerPlayerState::find_active_record(const GameAuthenticatedPeer& peer) {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (auto result = validate_peer(peer); !result) return result.error();
    const auto active = active_peers_.find(peer.peer);
    if (active == active_peers_.end() || active->second != peer.identity.account_id) {
        return invalid_state("Authenticated peer has no active authoritative player");
    }
    const auto player = players_.find(active->second);
    if (player == players_.end() || player->second.peer != peer.peer) {
        return invalid_state("Authenticated peer has inconsistent authoritative player state");
    }
    return &player->second;
}

snt::core::Expected<const GameServerPlayerState::PlayerRecord*>
GameServerPlayerState::find_active_record(const GameAuthenticatedPeer& peer) const {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (auto result = validate_peer(peer); !result) return result.error();
    const auto active = active_peers_.find(peer.peer);
    if (active == active_peers_.end() || active->second != peer.identity.account_id) {
        return invalid_state("Authenticated peer has no active authoritative player");
    }
    const auto player = players_.find(active->second);
    if (player == players_.end() || player->second.peer != peer.peer) {
        return invalid_state("Authenticated peer has inconsistent authoritative player state");
    }
    return &player->second;
}

snt::core::Expected<entt::entity> GameServerPlayerState::entity_for_record(
    const PlayerRecord& record) const {
    if (world_ == nullptr) return invalid_state("Dedicated server player state has no ECS World");
    const entt::entity entity = world_->find_entity_by_guid(record.entity_guid);
    if (entity == entt::null ||
        !world_->registry().all_of<GamePlayerIdentityComponent,
                                   GamePlayerDimensionComponent,
                                   GamePlayerRespawnPointComponent,
                                   snt::ecs::Position,
                                   GamePlayerInventory,
                                   GamePlayerEquipment,
                                   GamePlayerOrganState,
                                   GamePlayerToolState>(entity)) {
        return invalid_state("Authoritative player entity is absent or has invalid components");
    }
    return entity;
}

snt::core::Expected<GameServerPlayerSnapshot> GameServerPlayerState::snapshot_for_record(
    const PlayerRecord& record) const {
    auto entity = entity_for_record(record);
    if (!entity) return entity.error();
    const auto& identity = world_->get_component<GamePlayerIdentityComponent>(*entity);
    const auto& dimension = world_->get_component<GamePlayerDimensionComponent>(*entity);
    return GameServerPlayerSnapshot{
        .identity_provider = identity.provider,
        .account_id = identity.account_id,
        .display_name = identity.display_name,
        .entity_guid = record.entity_guid,
        .position = {
            .dimension_id = dimension.dimension_id,
            .position = world_->get_component<snt::ecs::Position>(*entity),
        },
        .equipment = world_->get_component<GamePlayerEquipment>(*entity),
        .peer = record.peer,
    };
}

snt::core::Expected<snt::ecs::EntityGuid> GameServerPlayerState::create_player_entity(
    const GameAuthenticatedPeer& peer, const GamePlayerPersistentState& state) {
    if (world_ == nullptr) return invalid_state("Dedicated server player state has no ECS World");
    const entt::entity entity = world_->create_entity();
    if (entity == entt::null) {
        return invalid_state("Dedicated server could not create an authoritative player entity");
    }
    world_->add_component<GamePlayerIdentityComponent>(entity, GamePlayerIdentityComponent{
        .provider = peer.identity.provider,
        .account_id = peer.identity.account_id,
        .display_name = peer.identity.display_name,
    });
    world_->add_component<GamePlayerDimensionComponent>(entity, GamePlayerDimensionComponent{
        .dimension_id = state.position.dimension_id,
    });
    world_->add_component<GamePlayerRespawnPointComponent>(entity, GamePlayerRespawnPointComponent{
        .position = state.respawn_point,
    });
    world_->add_component<snt::ecs::Position>(entity, state.position.position);
    world_->add_component<GamePlayerInventory>(entity, state.inventory);
    world_->add_component<GamePlayerEquipment>(entity, state.equipment);
    world_->add_component<GamePlayerOrganState>(entity, state.organs);
    world_->add_component<GamePlayerToolState>(entity, GamePlayerToolState{});
    return world_->guid_of(entity);
}

bool GameServerPlayerState::is_empty_stack(const GamePlayerItemStack& stack) noexcept {
    return is_empty_stack_value(stack);
}

void GameServerPlayerState::clear_stack(GamePlayerItemStack& stack) noexcept {
    stack.item_id.clear();
    stack.count = 0;
    stack.instance_data.clear();
}

bool GameServerPlayerState::stacks_can_merge(const GamePlayerItemStack& left,
                                              const GamePlayerItemStack& right) noexcept {
    return !left.item_id.empty() && left.item_id == right.item_id &&
           left.instance_data.empty() && right.instance_data.empty();
}

bool GameServerPlayerState::remove_items(GamePlayerInventory& inventory,
                                         const GamePlayerItemStack& stack) noexcept {
    int64_t available = 0;
    for (const GamePlayerItemStack& existing : inventory.slots) {
        if (existing.item_id == stack.item_id && existing.instance_data == stack.instance_data) {
            available += existing.count;
        }
    }
    if (available < stack.count) return false;

    int32_t remaining = stack.count;
    for (GamePlayerItemStack& existing : inventory.slots) {
        if (existing.item_id != stack.item_id || existing.instance_data != stack.instance_data ||
            remaining == 0) {
            continue;
        }
        const int32_t removed = std::min(existing.count, remaining);
        existing.count -= removed;
        remaining -= removed;
        if (existing.count == 0) clear_stack(existing);
    }
    return true;
}

bool GameServerPlayerState::add_items(GamePlayerInventory& inventory,
                                      const GamePlayerItemStack& stack) noexcept {
    if (!stack.instance_data.empty()) {
        for (GamePlayerItemStack& existing : inventory.slots) {
            if (!is_empty_stack(existing)) continue;
            existing = stack;
            return true;
        }
        return false;
    }

    int64_t remaining = stack.count;
    for (GamePlayerItemStack& existing : inventory.slots) {
        if (!stacks_can_merge(existing, stack) || existing.count >= inventory.max_stack_size) {
            continue;
        }
        const int32_t capacity = inventory.max_stack_size - existing.count;
        const int32_t added = static_cast<int32_t>(std::min<int64_t>(remaining, capacity));
        existing.count += added;
        remaining -= added;
        if (remaining == 0) return true;
    }
    for (GamePlayerItemStack& existing : inventory.slots) {
        if (!is_empty_stack(existing)) continue;
        const int32_t added = static_cast<int32_t>(
            std::min<int64_t>(remaining, inventory.max_stack_size));
        existing = {.item_id = stack.item_id, .count = added, .instance_data = {}};
        remaining -= added;
        if (remaining == 0) return true;
    }
    return false;
}

bool GameServerPlayerState::apply_slot_transfer(
    GamePlayerInventory& inventory,
    const GamePlayerInventorySlotTransfer& transfer) noexcept {
    if (transfer.source_slot >= inventory.slots.size() ||
        transfer.target_slot >= inventory.slots.size()) {
        return false;
    }

    GamePlayerItemStack& source = inventory.slots[transfer.source_slot];
    GamePlayerItemStack& target = inventory.slots[transfer.target_slot];
    if (source != transfer.expected_source || target != transfer.expected_target ||
        transfer.count > source.count) {
        return false;
    }

    if (is_empty_stack(target)) {
        target = source;
        target.count = transfer.count;
        source.count -= transfer.count;
        if (source.count == 0) clear_stack(source);
        return true;
    }
    if (stacks_can_merge(source, target)) {
        if (target.count > inventory.max_stack_size - transfer.count) return false;
        target.count += transfer.count;
        source.count -= transfer.count;
        if (source.count == 0) clear_stack(source);
        return true;
    }
    if (transfer.count != source.count) return false;
    std::swap(source, target);
    return true;
}

}  // namespace snt::game::replication
