// Dedicated-server shared-world interaction transaction implementation.

#define SNT_LOG_CHANNEL "game.server_player_interaction"
#include "game/server/game_server_player_interaction.h"

#include "core/error.h"
#include "ecs/world.h"
#include "game/client/machine_tick_system.h"
#include "game/server/game_server_player_death.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"
#include "game/simulation/machine_interaction_service.h"
#include "game/world/game_chunk.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/terrain_data.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxCollectedMachineStacks = 64;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int floor_divide(int value, int divisor) noexcept {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] int local_coordinate(int value, int divisor) noexcept {
    return value - floor_divide(value, divisor) * divisor;
}

struct TargetCell {
    GamePlayerWorldPosition position;
    ChunkKey chunk_key;
    snt::voxel::TerrainCell* cell = nullptr;
};

[[nodiscard]] GamePlayerWorldPosition make_world_position(
    const GameBlockInteractionCommand& command) {
    return {
        .dimension_id = command.dimension_id,
        .position = {.x = command.block_x, .y = command.block_y, .z = command.block_z},
    };
}

[[nodiscard]] snt::core::Expected<TargetCell> resolve_target(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command) {
    const GamePlayerWorldPosition position = make_world_position(command);
    auto reachable = player_state.is_target_reachable(peer, position);
    if (!reachable) return reachable.error();
    if (!*reachable) {
        return invalid_state("Client interaction target is outside the host basic reach limit");
    }

    constexpr int kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const ChunkKey key{
        position.dimension_id,
        floor_divide(position.position.x, kChunkSize),
        floor_divide(position.position.y, kChunkSize),
        floor_divide(position.position.z, kChunkSize),
    };
    snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) {
        return invalid_state("Client interaction target chunk is not loaded on the host");
    }
    const int local_x = local_coordinate(position.position.x, kChunkSize);
    const int local_y = local_coordinate(position.position.y, kChunkSize);
    const int local_z = local_coordinate(position.position.z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
        return invalid_state("Client interaction target cell is invalid in the loaded host chunk");
    }
    snt::voxel::TerrainCell* cell = &chunk->terrain.cell_at(local_x, local_y, local_z);
    if (command.expected_material != kGameNoExpectedTerrainMaterial &&
        command.expected_material != static_cast<uint16_t>(cell->material)) {
        return invalid_state("Client interaction target material changed before host commit");
    }
    return TargetCell{.position = position, .chunk_key = key, .cell = cell};
}

// Every non-bed sidecar root is opaque to this first block-edit migration.
// That prevents a client hint from orphaning a grave or a persistent machine.
[[nodiscard]] bool has_non_bed_sidecar_claim(const GameChunkSidecar& sidecar,
                                             const GamePlayerWorldPosition& position) {
    const auto at_target = [&position](int32_t x, int32_t y, int32_t z) {
        return x == position.position.x && y == position.position.y &&
               z == position.position.z;
    };
    if (std::any_of(sidecar.player_graves.begin(), sidecar.player_graves.end(),
                    [&at_target](const GamePlayerGraveRecord& record) {
                        return at_target(record.root_x, record.root_y, record.root_z);
                    })) {
        return true;
    }
    return std::any_of(sidecar.block_entities.begin(), sidecar.block_entities.end(),
                       [&at_target](const BlockEntityPlacement& placement) {
                           return at_target(placement.root_x, placement.root_y,
                                            placement.root_z);
                       });
}

[[nodiscard]] snt::core::Expected<snt::ecs::EntityGuid> find_machine_at(
    GameChunkSidecarRegistry& sidecars, const TargetCell& target) {
    GameChunkSidecar* sidecar = sidecars.get(target.chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Machine interaction target has no loaded game sidecar");
    }
    const auto placement = std::find_if(
        sidecar->block_entities.begin(), sidecar->block_entities.end(),
        [&target](const BlockEntityPlacement& value) {
            return value.entity_type == BlockEntityType::MACHINE &&
                   value.root_x == target.position.position.x &&
                   value.root_y == target.position.position.y &&
                   value.root_z == target.position.position.z;
        });
    if (placement == sidecar->block_entities.end()) {
        return invalid_state("Machine interaction target is not a machine anchor");
    }
    const auto runtime = std::find_if(
        sidecar->machine_runtime_records.begin(), sidecar->machine_runtime_records.end(),
        [&placement](const MachineRuntimePersistenceRecord& record) {
            return record.anchor_entity_id == placement->id;
        });
    if (runtime == sidecar->machine_runtime_records.end() || runtime->entity_guid == 0) {
        return invalid_state("Machine interaction target has no persistent runtime record");
    }
    return snt::ecs::EntityGuid{.value = runtime->entity_guid};
}

[[nodiscard]] snt::core::Expected<void> validate_catalog(
    GameServerPlayerInteractionConfig& config) {
    const uint32_t maximum_material =
        std::numeric_limits<snt::voxel::TerrainMaterialId>::max();
    if (config.air_material_id > maximum_material ||
        config.reserved_grave_material_id > maximum_material ||
        config.air_material_id == config.reserved_grave_material_id) {
        return invalid_argument("Game server block interaction material configuration is invalid");
    }

    std::set<std::string, std::less<>> item_ids;
    std::set<uint32_t> material_ids;
    for (const GameServerBlockDefinition& definition : config.block_definitions) {
        if (definition.item_id.empty() || definition.item_id.size() > kMaxGameItemIdBytes ||
            definition.item_id.find('\0') != std::string::npos ||
            definition.material_id > maximum_material ||
            definition.material_id == config.air_material_id ||
            definition.material_id == config.reserved_grave_material_id ||
            (definition.placement_flags &
             static_cast<uint32_t>(snt::voxel::TF_INDESTRUCTIBLE)) != 0 ||
            !item_ids.insert(definition.item_id).second ||
            !material_ids.insert(definition.material_id).second) {
            return invalid_argument("Game server block interaction catalog contains an invalid definition");
        }
    }
    return {};
}

}  // namespace

std::vector<GameServerBlockDefinition>
GameServerPlayerInteractionService::default_block_definitions() {
    const uint32_t solid_mineable_walkable =
        static_cast<uint32_t>(snt::voxel::TF_SOLID) |
        static_cast<uint32_t>(snt::voxel::TF_MINEABLE) |
        static_cast<uint32_t>(snt::voxel::TF_WALKABLE);
    return {
        {.item_id = "stone", .material_id = 1, .placement_flags = solid_mineable_walkable},
        {.item_id = "dirt", .material_id = 2, .placement_flags = solid_mineable_walkable},
        {.item_id = "sand", .material_id = 3,
         .placement_flags = solid_mineable_walkable |
                            static_cast<uint32_t>(snt::voxel::TF_GRAVITY_FALL)},
        {.item_id = "snow", .material_id = 4, .placement_flags = solid_mineable_walkable},
        {.item_id = "bed", .material_id = 5, .placement_flags = solid_mineable_walkable,
         .is_bed = true},
        {.item_id = "fire", .material_id = 6,
         .placement_flags = static_cast<uint32_t>(snt::voxel::TF_MINEABLE)},
    };
}

snt::core::Expected<std::unique_ptr<GameServerPlayerInteractionService>>
GameServerPlayerInteractionService::create(
    snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
    GameServerPlayerBedService& beds, MachineInteractionService& machine_interactions,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerPlayerInteractionEventSink* event_sink,
    GameServerPlayerInteractionConfig config) {
    if (config.block_definitions.empty()) {
        config.block_definitions = default_block_definitions();
    }
    if (auto result = validate_catalog(config); !result) return result.error();
    return std::unique_ptr<GameServerPlayerInteractionService>(
        new GameServerPlayerInteractionService(
            world, chunks, sidecars, player_state, beds, machine_interactions,
            checkpoint_sink, event_sink, std::move(config)));
}

GameServerPlayerInteractionService::GameServerPlayerInteractionService(
    snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
    GameServerPlayerBedService& beds, MachineInteractionService& machine_interactions,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerPlayerInteractionEventSink* event_sink,
    GameServerPlayerInteractionConfig config)
    : world_(&world), chunks_(&chunks), sidecars_(&sidecars), player_state_(&player_state),
      beds_(&beds), machine_interactions_(&machine_interactions),
      checkpoint_sink_(checkpoint_sink), event_sink_(event_sink), config_(std::move(config)) {}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_block_interaction(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (world_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr ||
        player_state_ == nullptr || beds_ == nullptr || machine_interactions_ == nullptr) {
        return invalid_state("Game server player interaction service is unavailable");
    }
    if (auto result = validate_game_block_interaction_command(command); !result) {
        return result.error();
    }
    switch (command.action) {
        case GameBlockInteractionAction::kMine:
            return apply_mine(peer, command, tick_index);
        case GameBlockInteractionAction::kPlace:
            return apply_place(peer, command, tick_index);
        case GameBlockInteractionAction::kUse:
            return apply_use(peer, command, tick_index);
        case GameBlockInteractionAction::kActivateMachine:
            return apply_machine_activation(peer, command, tick_index);
        case GameBlockInteractionAction::kCollectMachineOutput:
            return apply_machine_collect(peer, command, tick_index);
    }
    return invalid_argument("Game server player interaction action is invalid");
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_mine(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const uint32_t material = static_cast<uint32_t>(target->cell->material);
    if (material == config_.air_material_id || !target->cell->is_mineable() ||
        target->cell->is_indestructible()) {
        return invalid_state("Client mining target is not host-mineable");
    }
    const GameServerBlockDefinition* definition = find_block_by_material(material);
    if (definition == nullptr) {
        return invalid_state("Client mining target has no host block catalog definition");
    }
    GameChunkSidecar* sidecar = sidecars_->get(target->chunk_key);
    if (sidecar != nullptr && has_non_bed_sidecar_claim(*sidecar, target->position)) {
        return invalid_state("Client mining target owns a protected game sidecar anchor");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    if (definition->is_bed != *bed_present) {
        return invalid_state("Client mining target has inconsistent bed sidecar state");
    }

    const GamePlayerInventoryTransaction transaction{
        .additions = {{.item_id = definition->item_id, .count = 1}},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Client mining result does not fit the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();

    bool removed_bed = false;
    if (definition->is_bed) {
        if (auto result = beds_->on_bed_removed(target->position); !result) return result.error();
        removed_bed = true;
    }
    const snt::voxel::TerrainCell previous = *target->cell;
    *target->cell = {.material = static_cast<snt::voxel::TerrainMaterial>(config_.air_material_id)};
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        *target->cell = previous;
        if (removed_bed) {
            static_cast<void>(beds_->on_bed_placed(target->position));
        }
        auto error = result.error();
        error.with_context("GameServerPlayerInteractionService::apply_mine(inventory commit)");
        return error;
    }
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBlockMined,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = definition->item_id,
        .previous_material = material,
        .current_material = config_.air_material_id,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_place(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    if (static_cast<uint32_t>(target->cell->material) != config_.air_material_id ||
        target->cell->has_fluid()) {
        return invalid_state("Client placement target is not an empty host terrain cell");
    }
    const GameServerBlockDefinition* definition = find_block_by_item(command.selected_item_id);
    if (definition == nullptr) {
        return invalid_state("Client placement item is not in the host block catalog");
    }
    GameChunkSidecar* sidecar = sidecars_->get(target->chunk_key);
    if ((sidecar != nullptr && has_non_bed_sidecar_claim(*sidecar, target->position))) {
        return invalid_state("Client placement target owns a protected game sidecar anchor");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    if (*bed_present) {
        return invalid_state("Client placement target already owns a player bed anchor");
    }

    const GamePlayerInventoryTransaction transaction{
        .removals = {{.item_id = definition->item_id, .count = 1}},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Client placement item is absent from the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();

    bool added_bed = false;
    if (definition->is_bed) {
        if (auto result = beds_->on_bed_placed(target->position); !result) return result.error();
        added_bed = true;
    }
    const snt::voxel::TerrainCell previous = *target->cell;
    *target->cell = {
        .material = static_cast<snt::voxel::TerrainMaterial>(definition->material_id),
        .flags = definition->placement_flags,
    };
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        *target->cell = previous;
        if (added_bed) {
            static_cast<void>(beds_->on_bed_removed(target->position));
        }
        auto error = result.error();
        error.with_context("GameServerPlayerInteractionService::apply_place(inventory commit)");
        return error;
    }
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBlockPlaced,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = definition->item_id,
        .previous_material = config_.air_material_id,
        .current_material = definition->material_id,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_use(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const uint32_t material = static_cast<uint32_t>(target->cell->material);
    const GameServerBlockDefinition* definition = find_block_by_material(material);
    if (definition == nullptr || !definition->is_bed) {
        return invalid_state("Client use target has no supported host interaction");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    if (!*bed_present) {
        return invalid_state("Client use target has no matching player bed anchor");
    }
    if (auto result = beds_->set_respawn_point_from_bed(peer, target->position); !result) {
        return result.error();
    }
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBedUsed,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = definition->item_id,
        .previous_material = material,
        .current_material = material,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_machine_activation(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    auto machine_guid = find_machine_at(*sidecars_, *target);
    if (!machine_guid) return machine_guid.error();

    auto held_tags = player_state_->held_tool_tags_for_peer(peer);
    if (!held_tags) return held_tags.error();
    // selected_item_id is intentionally a trusted local tool hint in the
    // co-op model. Existing server-owned tags remain available to scripted
    // hosts, while clients without an equipment UI can still activate a tool
    // gated machine by selecting its content id.
    if (!command.selected_item_id.empty()) held_tags->push_back(command.selected_item_id);
    std::sort(held_tags->begin(), held_tags->end());
    held_tags->erase(std::unique(held_tags->begin(), held_tags->end()), held_tags->end());

    const MachineActivationContext context{
        .target_is_reachable = true,
        .cover_is_present =
            (command.client_hints & kGameBlockInteractionHintCover) != 0,
        .ignition_is_present =
            (command.client_hints & kGameBlockInteractionHintIgnition) != 0,
        .structure_is_valid =
            (command.client_hints & kGameBlockInteractionHintStructure) != 0,
        .held_tool_tags = std::move(*held_tags),
    };
    if (auto result = machine_interactions_->request_manual_activation(
            *world_, *machine_guid, context);
        !result) {
        return result.error();
    }
    const entt::entity entity = world_->find_entity_by_guid(*machine_guid);
    if (entity == entt::null ||
        !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
        return invalid_state("Machine activation target lost its live host runtime");
    }
    // Keep the stable account with this pending job, not a transient peer id.
    // MachineTickSystem copies it into the eventual completion event and the
    // current-format sidecar preserves it across controlled restarts.
    world_->get_component<MachineRuntimeComponent>(entity).job_owner_account_id =
        peer.identity.account_id;
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kMachineActivated,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_machine_collect(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    auto machine_guid = find_machine_at(*sidecars_, *target);
    if (!machine_guid) return machine_guid.error();
    const entt::entity entity = world_->find_entity_by_guid(*machine_guid);
    if (entity == entt::null ||
        !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
        return invalid_state("Machine collect target has no live host runtime");
    }
    MachineRuntimeComponent& runtime = world_->get_component<MachineRuntimeComponent>(entity);
    GamePlayerInventoryTransaction transaction;
    transaction.additions.reserve(runtime.output_slots.size());
    for (const MachineItemStack& output : runtime.output_slots) {
        if (output.empty()) continue;
        if (output.item_id.size() > kMaxGameItemIdBytes ||
            transaction.additions.size() == kMaxCollectedMachineStacks) {
            return invalid_state("Machine collect target has invalid output storage");
        }
        transaction.additions.push_back({.item_id = output.item_id, .count = output.count});
    }
    if (transaction.additions.empty()) {
        return invalid_state("Machine collect target has no output");
    }
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Machine output does not fit the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        return result.error();
    }
    for (MachineItemStack& output : runtime.output_slots) {
        output.item_id.clear();
        output.count = 0;
    }
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kMachineOutputCollected,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
    });
    return {};
}

const GameServerBlockDefinition* GameServerPlayerInteractionService::find_block_by_item(
    const std::string& item_id) const noexcept {
    const auto found = std::find_if(
        config_.block_definitions.begin(), config_.block_definitions.end(),
        [&item_id](const GameServerBlockDefinition& definition) {
            return definition.item_id == item_id;
        });
    return found == config_.block_definitions.end() ? nullptr : &*found;
}

const GameServerBlockDefinition* GameServerPlayerInteractionService::find_block_by_material(
    uint32_t material_id) const noexcept {
    const auto found = std::find_if(
        config_.block_definitions.begin(), config_.block_definitions.end(),
        [material_id](const GameServerBlockDefinition& definition) {
            return definition.material_id == material_id;
        });
    return found == config_.block_definitions.end() ? nullptr : &*found;
}

snt::core::Expected<void> GameServerPlayerInteractionService::mark_player_state_dirty(
    const GameAuthenticatedPeer& peer) {
    if (checkpoint_sink_ == nullptr) return {};
    return checkpoint_sink_->mark_player_state_dirty(peer);
}

void GameServerPlayerInteractionService::emit_event(
    GameServerPlayerInteractionEvent event) const {
    if (event_sink_ != nullptr) event_sink_->on_player_interaction(event);
}

}  // namespace snt::game::replication
