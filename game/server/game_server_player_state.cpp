// Dedicated-server authoritative player entity service implementation.

#define SNT_LOG_CHANNEL "game.server_player_state"
#include "game/server/game_server_player_state.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxDimensionIdBytes = 128;
constexpr size_t kMaxResourceTypeBytes = 64;
constexpr size_t kMaxResourceIdBytes = 256;
constexpr size_t kMaxResourceVariantBytes = 16u * 1024u;
constexpr size_t kMaxItemInstanceDataBytes = 16u * 1024u;
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
    return stack.is_empty();
}

[[nodiscard]] bool has_valid_nonempty_content_stack_shape(
    const GamePlayerItemStack& stack) noexcept {
    if (!stack.is_valid_item() ||
        stack.resource.key.type.size() > kMaxResourceTypeBytes ||
        stack.resource.key.id.size() > kMaxResourceIdBytes ||
        stack.resource.key.variant.size() > kMaxResourceVariantBytes ||
        stack.instance_data.size() > kMaxItemInstanceDataBytes ||
        stack.instance_data.find('\0') != std::string::npos) {
        return false;
    }
    return stack.instance_data.empty() || stack.resource.amount == 1;
}

[[nodiscard]] bool has_valid_nonempty_runtime_stack_shape(
    const GamePlayerRuntimeItemStack& stack) noexcept {
    return stack.is_valid_item() &&
           stack.instance_data.size() <= kMaxItemInstanceDataBytes &&
           stack.instance_data.find('\0') == std::string::npos;
}

[[nodiscard]] std::optional<GamePlayerRuntimeItemStack> resolve_runtime_stack(
    const GamePlayerItemStack& stack,
    const IResourceKeyResolver& resolver) {
    if (stack.is_empty()) return GamePlayerRuntimeItemStack{};
    if (!stack.is_valid_item()) return std::nullopt;
    const auto resource = resolve_resource_stack(stack.resource, resolver);
    if (!resource) return std::nullopt;
    return GamePlayerRuntimeItemStack{
        .resource = *resource,
        .instance_data = stack.instance_data,
    };
}

[[nodiscard]] std::optional<GamePlayerItemStack> resolve_content_player_stack(
    const GamePlayerRuntimeItemStack& stack,
    const IResourceKeyResolver& resolver) {
    if (stack.is_empty()) return GamePlayerItemStack{};
    if (!stack.is_valid_item()) return std::nullopt;
    const auto resource = resolve_content_stack(stack.resource, resolver);
    if (!resource || !resource->is_item()) return std::nullopt;
    return GamePlayerItemStack{
        .resource = *resource,
        .instance_data = stack.instance_data,
    };
}

}  // namespace

struct GameServerPlayerState::RuntimeInventoryTransaction {
    std::vector<GamePlayerRuntimeItemStack> removals;
    std::vector<GamePlayerRuntimeItemStack> additions;
};

struct GameServerPlayerState::RuntimeInventorySlotTransfer {
    uint32_t source_slot = 0;
    uint32_t target_slot = 0;
    int32_t count = 0;
    GamePlayerRuntimeItemStack expected_source;
    GamePlayerRuntimeItemStack expected_target;
};

struct GameServerPlayerState::RuntimeInventorySlotMutation {
    uint32_t slot = 0;
    GamePlayerRuntimeItemStack expected;
    GamePlayerRuntimeItemStack replacement;
};

snt::core::Expected<std::unique_ptr<GameServerPlayerState>> GameServerPlayerState::create(
    snt::ecs::World& world, GameServerPlayerStateConfig config) {
    if (!config.resource_runtime_index.key_context().is_valid()) {
        return invalid_argument(
            "Dedicated server player state requires a valid resource runtime snapshot");
    }
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
    : world_(&world), config_(std::move(config)),
      resource_runtime_index_(config_.resource_runtime_index) {}

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
    return resolve_content_inventory(
        world_->get_component<GamePlayerRuntimeInventory>(*entity));
}

snt::core::Expected<GamePlayerEquipment> GameServerPlayerState::equipment_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    const auto& inventory = world_->get_component<GamePlayerRuntimeInventory>(*entity);
    return resolve_content_equipment(
        world_->get_component<GamePlayerRuntimeEquipment>(*entity),
        inventory.resource_runtime_index);
}

snt::core::Expected<void> GameServerPlayerState::rebind_resource_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (auto result = prepare_resource_runtime_snapshot(std::move(next_snapshot)); !result) {
        return result.error();
    }
    commit_resource_runtime_snapshot();
    return {};
}

snt::core::Expected<void> GameServerPlayerState::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (stopped_) return invalid_state("Dedicated server player state is stopped");
    if (!next_snapshot.key_context().is_valid()) {
        return invalid_argument("Player resource rebind requires a valid next runtime snapshot");
    }
    if (pending_resource_snapshot_rebind_) {
        return invalid_state("Dedicated server player resource rebind is already prepared");
    }
    if (resource_runtime_index_.key_context().matches(next_snapshot.key_context())) return {};

    PendingResourceSnapshotRebind pending;
    pending.next_snapshot = std::move(next_snapshot);
    pending.players.reserve(players_.size());
    for (const auto& [account_id, record] : players_) {
        static_cast<void>(account_id);
        auto entity = entity_for_record(record);
        if (!entity) return entity.error();
        const auto& current_inventory =
            world_->get_component<GamePlayerRuntimeInventory>(*entity);
        const auto& current_equipment =
            world_->get_component<GamePlayerRuntimeEquipment>(*entity);
        if (auto result = validate_runtime_inventory(current_inventory); !result) {
            return result.error();
        }
        if (auto result = validate_runtime_equipment(current_equipment); !result) {
            return result.error();
        }
        auto content_inventory = resolve_content_inventory(current_inventory);
        if (!content_inventory) return content_inventory.error();
        auto content_equipment = resolve_content_equipment(
            current_equipment, current_inventory.resource_runtime_index);
        if (!content_equipment) return content_equipment.error();
        auto next_inventory = resolve_runtime_inventory(*content_inventory, pending.next_snapshot);
        if (!next_inventory) return next_inventory.error();
        auto next_equipment = resolve_runtime_equipment(*content_equipment, pending.next_snapshot);
        if (!next_equipment) return next_equipment.error();
        pending.players.push_back({
            .entity = *entity,
            .inventory = std::move(*next_inventory),
            .equipment = std::move(*next_equipment),
        });
    }
    pending_resource_snapshot_rebind_ = std::move(pending);
    return {};
}

void GameServerPlayerState::commit_resource_runtime_snapshot() noexcept {
    if (!pending_resource_snapshot_rebind_) return;
    PendingResourceSnapshotRebind pending = std::move(*pending_resource_snapshot_rebind_);
    pending_resource_snapshot_rebind_.reset();
    resource_runtime_index_ = std::move(pending.next_snapshot);
    config_.resource_runtime_index = resource_runtime_index_;
    for (PendingResourceSnapshotRebind::Player& player : pending.players) {
        world_->get_component<GamePlayerRuntimeInventory>(player.entity) =
            std::move(player.inventory);
        world_->get_component<GamePlayerRuntimeEquipment>(player.entity) =
            std::move(player.equipment);
    }
    SNT_LOG_INFO("Rebound %zu active player inventories to resource snapshot generation %llu",
                 pending.players.size(),
                 static_cast<unsigned long long>(resource_runtime_index_.generation()));
}

void GameServerPlayerState::cancel_resource_runtime_snapshot() noexcept {
    pending_resource_snapshot_rebind_.reset();
}

snt::core::Expected<std::string> GameServerPlayerState::main_hand_item_id_for_peer(
    const GameAuthenticatedPeer& peer) const {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    const auto& inventory = world_->get_component<GamePlayerRuntimeInventory>(*entity);
    const auto& equipment = world_->get_component<GamePlayerRuntimeEquipment>(*entity);
    const auto stack = resolve_content_player_stack(
        equipment.slots[static_cast<size_t>(GamePlayerEquipmentSlot::kMainHand)],
        inventory.resource_runtime_index);
    if (!stack) {
        return invalid_state("Authoritative player main-hand stack cannot resolve its content key");
    }
    return stack->resource.key.id;
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
    const auto& inventory = world_->get_component<GamePlayerRuntimeInventory>(*entity);
    const auto& equipment = world_->get_component<GamePlayerRuntimeEquipment>(*entity);
    auto content_inventory = resolve_content_inventory(inventory);
    if (!content_inventory) return content_inventory.error();
    auto content_equipment = resolve_content_equipment(
        equipment, inventory.resource_runtime_index);
    if (!content_equipment) return content_equipment.error();
    return GamePlayerPersistentState{
        .position = {
            .dimension_id = dimension.dimension_id,
            .position = world_->get_component<snt::ecs::Position>(*entity),
        },
        .respawn_point = world_->get_component<GamePlayerRespawnPointComponent>(*entity).position,
        .inventory = std::move(*content_inventory),
        .equipment = std::move(*content_equipment),
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

snt::core::Expected<void> GameServerPlayerState::set_authoritative_equipment(
    const GameAuthenticatedPeer& peer, GamePlayerEquipment equipment) {
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();
    const auto& inventory = world_->get_component<GamePlayerRuntimeInventory>(*entity);
    auto runtime = resolve_runtime_equipment(equipment, inventory.resource_runtime_index);
    if (!runtime) return runtime.error();
    if (auto result = validate_runtime_equipment(*runtime); !result) return result.error();
    world_->get_component<GamePlayerRuntimeEquipment>(*entity) = std::move(*runtime);
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

snt::core::Expected<bool> GameServerPlayerState::is_point_reachable(
    const GameAuthenticatedPeer& peer, std::string_view dimension_id,
    float position_x, float position_y, float position_z) const {
    if (dimension_id.empty() || dimension_id.size() > kMaxDimensionIdBytes ||
        dimension_id.find('\0') != std::string_view::npos || !std::isfinite(position_x) ||
        !std::isfinite(position_y) || !std::isfinite(position_z)) {
        return invalid_argument("Authoritative dynamic target position is invalid");
    }
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    const auto& dimension = world_->get_component<GamePlayerDimensionComponent>(*entity);
    if (dimension.dimension_id != dimension_id) return false;
    const auto& position = world_->get_component<snt::ecs::Position>(*entity);
    const double reach = static_cast<double>(config_.interaction_reach_blocks);
    const double delta_x = static_cast<double>(position_x) - static_cast<double>(position.x);
    const double delta_y = static_cast<double>(position_y) - static_cast<double>(position.y);
    const double delta_z = static_cast<double>(position_z) - static_cast<double>(position.z);
    if (std::abs(delta_x) > reach || std::abs(delta_y) > reach || std::abs(delta_z) > reach) {
        return false;
    }
    return delta_x * delta_x + delta_y * delta_y + delta_z * delta_z <= reach * reach;
}

snt::core::Expected<void> GameServerPlayerState::apply_inventory_transaction(
    const GameAuthenticatedPeer& peer, const GamePlayerInventoryTransaction& transaction) {
    auto runtime_transaction = resolve_runtime_inventory_transaction(transaction);
    if (!runtime_transaction) return runtime_transaction.error();

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    for (const GamePlayerRuntimeItemStack& removal : runtime_transaction->removals) {
        const bool removed = remove_items(candidate, removal);
        if (!removed) {
            return invalid_state("Authoritative player inventory changed during transaction apply");
        }
    }
    for (const GamePlayerRuntimeItemStack& addition : runtime_transaction->additions) {
        const bool added = add_items(candidate, addition);
        if (!added) {
            return invalid_state("Authoritative player inventory changed during transaction apply");
        }
    }
    world_->get_component<GamePlayerRuntimeInventory>(*entity) = std::move(candidate);
    return {};
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_transaction(
    const GameAuthenticatedPeer& peer, const GamePlayerInventoryTransaction& transaction) const {
    auto runtime_transaction = resolve_runtime_inventory_transaction(transaction);
    if (!runtime_transaction) return runtime_transaction.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    for (const GamePlayerRuntimeItemStack& removal : runtime_transaction->removals) {
        if (!remove_items(candidate, removal)) {
            return false;
        }
    }
    for (const GamePlayerRuntimeItemStack& addition : runtime_transaction->additions) {
        if (!add_items(candidate, addition)) {
            return false;
        }
    }
    return true;
}

snt::core::Expected<GamePlayerInventory> GameServerPlayerState::apply_inventory_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GamePlayerInventorySlotTransfer& transfer) {
    auto runtime_transfer = resolve_runtime_inventory_slot_transfer(transfer);
    if (!runtime_transfer) return runtime_transfer.error();

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    if (!apply_slot_transfer(candidate, *runtime_transfer)) {
        return invalid_state("Authoritative player inventory rejected the requested slot transfer");
    }
    auto content = resolve_content_inventory(candidate);
    if (!content) return content.error();
    world_->get_component<GamePlayerRuntimeInventory>(*entity) = std::move(candidate);
    return content;
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GamePlayerInventorySlotTransfer& transfer) const {
    auto runtime_transfer = resolve_runtime_inventory_slot_transfer(transfer);
    if (!runtime_transfer) return runtime_transfer.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    return apply_slot_transfer(candidate, *runtime_transfer);
}

snt::core::Expected<void> GameServerPlayerState::apply_inventory_slot_mutations(
    const GameAuthenticatedPeer& peer,
    std::span<const GamePlayerInventorySlotMutation> mutations) {
    auto runtime_mutations = resolve_runtime_inventory_slot_mutations(mutations);
    if (!runtime_mutations) return runtime_mutations.error();

    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    for (const RuntimeInventorySlotMutation& mutation : *runtime_mutations) {
        if (mutation.slot >= candidate.slots.size() ||
            candidate.slots[mutation.slot] != mutation.expected) {
            return invalid_state(
                "Authoritative player inventory changed during conditional slot mutation apply");
        }
        candidate.slots[mutation.slot] = mutation.replacement;
    }
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    world_->get_component<GamePlayerRuntimeInventory>(*entity) = std::move(candidate);
    return {};
}

snt::core::Expected<bool> GameServerPlayerState::can_apply_inventory_slot_mutations(
    const GameAuthenticatedPeer& peer,
    std::span<const GamePlayerInventorySlotMutation> mutations) const {
    auto runtime_mutations = resolve_runtime_inventory_slot_mutations(mutations);
    if (!runtime_mutations) return runtime_mutations.error();
    auto record = find_active_record(peer);
    if (!record) return record.error();
    auto entity = entity_for_record(**record);
    if (!entity) return entity.error();

    GamePlayerRuntimeInventory candidate =
        world_->get_component<GamePlayerRuntimeInventory>(*entity);
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    for (const RuntimeInventorySlotMutation& mutation : *runtime_mutations) {
        if (mutation.slot >= candidate.slots.size() ||
            candidate.slots[mutation.slot] != mutation.expected) {
            return false;
        }
        candidate.slots[mutation.slot] = mutation.replacement;
    }
    if (auto result = validate_runtime_inventory(candidate); !result) return result.error();
    return true;
}

void GameServerPlayerState::shutdown() noexcept {
    if (stopped_) return;
    stopped_ = true;
    cancel_resource_runtime_snapshot();
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

snt::core::Expected<void> GameServerPlayerState::validate_content_inventory(
    const GamePlayerInventory& inventory) const {
    if (inventory.max_slots != config_.inventory_slots ||
        inventory.max_stack_size != config_.inventory_max_stack_size) {
        return invalid_state("Authoritative player inventory has incompatible capacity settings");
    }
    if (inventory.slots.size() != inventory.max_slots) {
        return invalid_state("Authoritative player inventory does not have fixed slot storage");
    }
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (stack.is_empty()) continue;
        if (!has_valid_nonempty_content_stack_shape(stack) ||
            stack.resource.amount > inventory.max_stack_size) {
            return invalid_state("Authoritative player inventory contains an invalid item stack");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_content_equipment(
    const GamePlayerEquipment& equipment) const {
    for (const GamePlayerItemStack& stack : equipment.slots) {
        if (stack.is_empty()) continue;
        if (!has_valid_nonempty_content_stack_shape(stack) || stack.resource.amount != 1) {
            return invalid_state("Authoritative player equipment contains an invalid item stack");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_runtime_inventory(
    const GamePlayerRuntimeInventory& inventory) const {
    if (!inventory.resource_runtime_index.key_context().is_valid() ||
        !inventory.resource_runtime_index.key_context().matches(
            resource_runtime_index_.key_context())) {
        return invalid_state(
            "Authoritative player inventory is bound to a stale resource runtime snapshot");
    }
    if (inventory.max_slots != config_.inventory_slots ||
        inventory.max_stack_size != config_.inventory_max_stack_size) {
        return invalid_state("Authoritative player inventory has incompatible capacity settings");
    }
    if (inventory.slots.size() != inventory.max_slots) {
        return invalid_state("Authoritative player inventory does not have fixed slot storage");
    }
    for (const GamePlayerRuntimeItemStack& stack : inventory.slots) {
        if (is_empty_stack(stack)) continue;
        if (!has_valid_nonempty_runtime_stack_shape(stack) ||
            stack.resource.amount > inventory.max_stack_size) {
            return invalid_state("Authoritative player inventory contains an invalid runtime stack");
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_runtime_equipment(
    const GamePlayerRuntimeEquipment& equipment) const {
    for (const GamePlayerRuntimeItemStack& stack : equipment.slots) {
        if (is_empty_stack(stack)) continue;
        if (!has_valid_nonempty_runtime_stack_shape(stack) || stack.resource.amount != 1) {
            return invalid_state("Authoritative player equipment contains an invalid runtime stack");
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
    if (config_.organ_state_codec != nullptr) {
        auto result = config_.organ_state_codec->validate_organ_state(organs);
        if (!result) {
            auto error = result.error();
            error.with_context("GameServerPlayerState::validate_organ_state");
            return error;
        }
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerState::validate_persistent_state(
    const GamePlayerPersistentState& state) const {
    if (auto result = validate_position(state.position); !result) return result.error();
    if (state.respawn_point.has_value()) {
        if (auto result = validate_position(*state.respawn_point); !result) return result.error();
    }
    if (auto result = validate_content_inventory(state.inventory); !result) return result.error();
    if (auto result = validate_content_equipment(state.equipment); !result) return result.error();
    return validate_organ_state(state.organs);
}

snt::core::Expected<void> GameServerPlayerState::validate_inventory_transaction(
    const GamePlayerInventoryTransaction& transaction) const {
    const size_t count = transaction.removals.size() + transaction.additions.size();
    if (count == 0 || count > kMaxInventoryTransactionEntries) {
        return invalid_argument("Authoritative player inventory transaction has an invalid operation count");
    }
    const auto validate_stack = [](const GamePlayerItemStack& stack) {
        return has_valid_nonempty_content_stack_shape(stack);
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
    if (!has_valid_nonempty_content_stack_shape(transfer.expected_source)) {
        return invalid_argument("Authoritative player slot transfer has an invalid expected source");
    }
    if (!is_empty_stack_value(transfer.expected_target) &&
        !has_valid_nonempty_content_stack_shape(transfer.expected_target)) {
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
            return is_empty_stack_value(stack) || has_valid_nonempty_content_stack_shape(stack);
        };
        if (!valid_stack(mutation.expected) || !valid_stack(mutation.replacement)) {
            return invalid_argument(
                "Authoritative player conditional slot mutation has an invalid stack value");
        }
    }
    return {};
}

snt::core::Expected<GamePlayerRuntimeInventory>
GameServerPlayerState::resolve_runtime_inventory(
    const GamePlayerInventory& inventory,
    const ResourceRuntimeIndex::Snapshot& resource_index) const {
    if (auto result = validate_content_inventory(inventory); !result) return result.error();
    if (!resource_index.key_context().is_valid()) {
        return invalid_state("Player inventory cannot resolve through an invalid resource snapshot");
    }

    GamePlayerRuntimeInventory runtime{
        .resource_runtime_index = resource_index,
        .slots = std::vector<GamePlayerRuntimeItemStack>(inventory.slots.size()),
        .max_slots = inventory.max_slots,
        .max_stack_size = inventory.max_stack_size,
    };
    for (size_t index = 0; index < inventory.slots.size(); ++index) {
        const auto stack = resolve_runtime_stack(inventory.slots[index], resource_index);
        if (!stack) {
            return invalid_state("Player inventory contains an unresolved content resource key");
        }
        runtime.slots[index] = *stack;
    }
    return runtime;
}

snt::core::Expected<GamePlayerRuntimeEquipment>
GameServerPlayerState::resolve_runtime_equipment(
    const GamePlayerEquipment& equipment,
    const ResourceRuntimeIndex::Snapshot& resource_index) const {
    if (auto result = validate_content_equipment(equipment); !result) return result.error();
    if (!resource_index.key_context().is_valid()) {
        return invalid_state("Player equipment cannot resolve through an invalid resource snapshot");
    }

    GamePlayerRuntimeEquipment runtime;
    for (size_t index = 0; index < equipment.slots.size(); ++index) {
        const auto stack = resolve_runtime_stack(equipment.slots[index], resource_index);
        if (!stack) {
            return invalid_state("Player equipment contains an unresolved content resource key");
        }
        runtime.slots[index] = *stack;
    }
    return runtime;
}

snt::core::Expected<GamePlayerInventory> GameServerPlayerState::resolve_content_inventory(
    const GamePlayerRuntimeInventory& inventory) const {
    if (!inventory.resource_runtime_index.key_context().is_valid()) {
        return invalid_state("Player runtime inventory has no resource snapshot");
    }

    GamePlayerInventory content{
        .slots = std::vector<GamePlayerItemStack>(inventory.slots.size()),
        .max_slots = inventory.max_slots,
        .max_stack_size = inventory.max_stack_size,
    };
    for (size_t index = 0; index < inventory.slots.size(); ++index) {
        const auto stack = resolve_content_player_stack(
            inventory.slots[index], inventory.resource_runtime_index);
        if (!stack) {
            return invalid_state("Player runtime inventory contains an unresolved compact key");
        }
        content.slots[index] = *stack;
    }
    return content;
}

snt::core::Expected<GamePlayerEquipment> GameServerPlayerState::resolve_content_equipment(
    const GamePlayerRuntimeEquipment& equipment,
    const ResourceRuntimeIndex::Snapshot& resource_index) const {
    if (!resource_index.key_context().is_valid()) {
        return invalid_state("Player runtime equipment has no resource snapshot");
    }

    GamePlayerEquipment content;
    for (size_t index = 0; index < equipment.slots.size(); ++index) {
        const auto stack = resolve_content_player_stack(equipment.slots[index], resource_index);
        if (!stack) {
            return invalid_state("Player runtime equipment contains an unresolved compact key");
        }
        content.slots[index] = *stack;
    }
    return content;
}

snt::core::Expected<GameServerPlayerState::RuntimeInventoryTransaction>
GameServerPlayerState::resolve_runtime_inventory_transaction(
    const GamePlayerInventoryTransaction& transaction) const {
    if (auto result = validate_inventory_transaction(transaction); !result) return result.error();

    RuntimeInventoryTransaction runtime;
    runtime.removals.reserve(transaction.removals.size());
    runtime.additions.reserve(transaction.additions.size());
    const auto resolve = [this](const GamePlayerItemStack& stack)
        -> snt::core::Expected<GamePlayerRuntimeItemStack> {
        const auto resolved = resolve_runtime_stack(stack, resource_runtime_index_);
        if (!resolved) {
            return invalid_state("Player inventory transaction contains an unresolved content key");
        }
        return *resolved;
    };
    for (const GamePlayerItemStack& stack : transaction.removals) {
        auto resolved = resolve(stack);
        if (!resolved) return resolved.error();
        runtime.removals.push_back(std::move(*resolved));
    }
    for (const GamePlayerItemStack& stack : transaction.additions) {
        auto resolved = resolve(stack);
        if (!resolved) return resolved.error();
        runtime.additions.push_back(std::move(*resolved));
    }
    return runtime;
}

snt::core::Expected<GameServerPlayerState::RuntimeInventorySlotTransfer>
GameServerPlayerState::resolve_runtime_inventory_slot_transfer(
    const GamePlayerInventorySlotTransfer& transfer) const {
    if (auto result = validate_inventory_slot_transfer(transfer); !result) return result.error();
    const auto source = resolve_runtime_stack(transfer.expected_source, resource_runtime_index_);
    const auto target = resolve_runtime_stack(transfer.expected_target, resource_runtime_index_);
    if (!source || !target) {
        return invalid_state("Player inventory slot transfer contains an unresolved content key");
    }
    return RuntimeInventorySlotTransfer{
        .source_slot = transfer.source_slot,
        .target_slot = transfer.target_slot,
        .count = transfer.count,
        .expected_source = *source,
        .expected_target = *target,
    };
}

snt::core::Expected<std::vector<GameServerPlayerState::RuntimeInventorySlotMutation>>
GameServerPlayerState::resolve_runtime_inventory_slot_mutations(
    std::span<const GamePlayerInventorySlotMutation> mutations) const {
    if (auto result = validate_inventory_slot_mutations(mutations); !result) return result.error();
    std::vector<RuntimeInventorySlotMutation> runtime;
    runtime.reserve(mutations.size());
    for (const GamePlayerInventorySlotMutation& mutation : mutations) {
        const auto expected = resolve_runtime_stack(mutation.expected, resource_runtime_index_);
        const auto replacement = resolve_runtime_stack(
            mutation.replacement, resource_runtime_index_);
        if (!expected || !replacement) {
            return invalid_state("Player inventory slot mutation contains an unresolved content key");
        }
        runtime.push_back({
            .slot = mutation.slot,
            .expected = *expected,
            .replacement = *replacement,
        });
    }
    return runtime;
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
                                   GamePlayerRuntimeInventory,
                                   GamePlayerRuntimeEquipment,
                                   GamePlayerOrganState>(entity)) {
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
    const auto& inventory = world_->get_component<GamePlayerRuntimeInventory>(*entity);
    auto equipment = resolve_content_equipment(
        world_->get_component<GamePlayerRuntimeEquipment>(*entity),
        inventory.resource_runtime_index);
    if (!equipment) return equipment.error();
    return GameServerPlayerSnapshot{
        .identity_provider = identity.provider,
        .account_id = identity.account_id,
        .display_name = identity.display_name,
        .entity_guid = record.entity_guid,
        .position = {
            .dimension_id = dimension.dimension_id,
            .position = world_->get_component<snt::ecs::Position>(*entity),
        },
        .equipment = std::move(*equipment),
        .peer = record.peer,
    };
}

snt::core::Expected<snt::ecs::EntityGuid> GameServerPlayerState::create_player_entity(
    const GameAuthenticatedPeer& peer, const GamePlayerPersistentState& state) {
    if (world_ == nullptr) return invalid_state("Dedicated server player state has no ECS World");
    auto inventory = resolve_runtime_inventory(state.inventory, resource_runtime_index_);
    if (!inventory) return inventory.error();
    auto equipment = resolve_runtime_equipment(state.equipment, resource_runtime_index_);
    if (!equipment) return equipment.error();
    if (auto result = validate_runtime_inventory(*inventory); !result) return result.error();
    if (auto result = validate_runtime_equipment(*equipment); !result) return result.error();
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
    world_->add_component<GamePlayerRuntimeInventory>(entity, std::move(*inventory));
    world_->add_component<GamePlayerRuntimeEquipment>(entity, std::move(*equipment));
    world_->add_component<GamePlayerOrganState>(entity, state.organs);
    return world_->guid_of(entity);
}

bool GameServerPlayerState::is_empty_stack(const GamePlayerRuntimeItemStack& stack) noexcept {
    return stack.is_empty();
}

void GameServerPlayerState::clear_stack(GamePlayerRuntimeItemStack& stack) noexcept {
    stack.clear();
}

bool GameServerPlayerState::stacks_can_merge(const GamePlayerRuntimeItemStack& left,
                                              const GamePlayerRuntimeItemStack& right) noexcept {
    return left.resource.is_valid() && left.resource.key == right.resource.key &&
           left.instance_data.empty() && right.instance_data.empty();
}

bool GameServerPlayerState::remove_items(GamePlayerRuntimeInventory& inventory,
                                         const GamePlayerRuntimeItemStack& stack) noexcept {
    int64_t available = 0;
    for (const GamePlayerRuntimeItemStack& existing : inventory.slots) {
        if (existing.resource.key == stack.resource.key &&
            existing.instance_data == stack.instance_data) {
            available += existing.resource.amount;
        }
    }
    if (available < stack.resource.amount) return false;

    int64_t remaining = stack.resource.amount;
    for (GamePlayerRuntimeItemStack& existing : inventory.slots) {
        if (existing.resource.key != stack.resource.key ||
            existing.instance_data != stack.instance_data ||
            remaining == 0) {
            continue;
        }
        const int64_t removed = std::min(existing.resource.amount, remaining);
        existing.resource.amount -= removed;
        remaining -= removed;
        if (existing.resource.amount == 0) clear_stack(existing);
    }
    return true;
}

bool GameServerPlayerState::add_items(GamePlayerRuntimeInventory& inventory,
                                      const GamePlayerRuntimeItemStack& stack) noexcept {
    if (!stack.instance_data.empty()) {
        for (GamePlayerRuntimeItemStack& existing : inventory.slots) {
            if (!is_empty_stack(existing)) continue;
            existing = stack;
            return true;
        }
        return false;
    }

    int64_t remaining = stack.resource.amount;
    for (GamePlayerRuntimeItemStack& existing : inventory.slots) {
        if (!stacks_can_merge(existing, stack) ||
            existing.resource.amount >= inventory.max_stack_size) {
            continue;
        }
        const int64_t capacity = inventory.max_stack_size - existing.resource.amount;
        const int64_t added = std::min(remaining, capacity);
        existing.resource.amount += added;
        remaining -= added;
        if (remaining == 0) return true;
    }
    for (GamePlayerRuntimeItemStack& existing : inventory.slots) {
        if (!is_empty_stack(existing)) continue;
        const int64_t added = std::min<int64_t>(remaining, inventory.max_stack_size);
        existing = {
            .resource = {
                .key = stack.resource.key,
                .amount = added,
            },
            .instance_data = {},
        };
        remaining -= added;
        if (remaining == 0) return true;
    }
    return false;
}

bool GameServerPlayerState::apply_slot_transfer(
    GamePlayerRuntimeInventory& inventory,
    const RuntimeInventorySlotTransfer& transfer) noexcept {
    if (transfer.source_slot >= inventory.slots.size() ||
        transfer.target_slot >= inventory.slots.size()) {
        return false;
    }

    GamePlayerRuntimeItemStack& source = inventory.slots[transfer.source_slot];
    GamePlayerRuntimeItemStack& target = inventory.slots[transfer.target_slot];
    if (source != transfer.expected_source || target != transfer.expected_target ||
        transfer.count > source.resource.amount) {
        return false;
    }

    if (is_empty_stack(target)) {
        target = source;
        target.resource.amount = transfer.count;
        source.resource.amount -= transfer.count;
        if (source.resource.amount == 0) clear_stack(source);
        return true;
    }
    if (stacks_can_merge(source, target)) {
        if (target.resource.amount > inventory.max_stack_size - transfer.count) return false;
        target.resource.amount += transfer.count;
        source.resource.amount -= transfer.count;
        if (source.resource.amount == 0) clear_stack(source);
        return true;
    }
    if (transfer.count != source.resource.amount) return false;
    std::swap(source, target);
    return true;
}

}  // namespace snt::game::replication
