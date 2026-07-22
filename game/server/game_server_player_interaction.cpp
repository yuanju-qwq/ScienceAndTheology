// Dedicated-server shared-world interaction transaction implementation.

#define SNT_LOG_CHANNEL "game.server_player_interaction"
#include "game/server/game_server_player_interaction.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"
#include "game/server/game_server_player_death.h"
#include "game/server/game_server_inventory_replication.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/simulation/block_physics_events.h"
#include "game/simulation/automation_controller_persistence.h"
#include "game/simulation/automation_controller_runtime.h"
#include "game/simulation/crop_growth_system.h"
#include "game/simulation/game_fluid_system_events.h"
#include "game/simulation/machine_interaction_service.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/world/game_chunk.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/terrain_data.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <functional>
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
    if (config.worldgen_config == nullptr) {
        return invalid_argument("Game server block interaction requires a finalized worldgen snapshot");
    }
    const WorldGenConfigSnapshot& worldgen = *config.worldgen_config;
    const uint32_t air_material_id = worldgen.roles.air;
    if (!worldgen.has_material(worldgen.roles.air) ||
        config.reserved_grave_material_id == air_material_id) {
        return invalid_argument("Game server block interaction material configuration is invalid");
    }

    std::set<std::string, std::less<>> item_ids;
    std::set<std::string, std::less<>> material_keys;
    for (GameServerBlockDefinition& definition : config.block_definitions) {
        auto normalized_material_key = normalize_terrain_material_key(definition.material_key);
        if (!normalized_material_key) return normalized_material_key.error();
        definition.material_key = std::move(*normalized_material_key);
        const TerrainMaterialDef* material = worldgen.find_material(definition.material_key);
        if (definition.item_id.empty() || definition.item_id.size() > kMaxGameItemIdBytes ||
            definition.item_id.find('\0') != std::string::npos ||
            material == nullptr || material->id == air_material_id ||
            material->id == config.reserved_grave_material_id ||
            (material->flags & static_cast<uint32_t>(snt::voxel::TF_INDESTRUCTIBLE)) != 0 ||
            !item_ids.insert(definition.item_id).second ||
            !material_keys.insert(definition.material_key).second) {
            return invalid_argument("Game server block interaction catalog contains an invalid definition");
        }
    }
    return {};
}

// Content owns machine/item meaning, while this service owns the world-side
// terrain catalog. Validate their boundary once before accepting commands so
// an invalid package cannot wait until a player tries to place an item.
[[nodiscard]] snt::core::Expected<void> validate_machine_placement_catalog(
    const GameContentRegistry& content,
    const GameServerPlayerInteractionConfig& config) {
    if (auto result = content.validate_machine_placement_references(); !result) {
        return result.error();
    }
    const WorldGenConfigSnapshot& worldgen = *config.worldgen_config;
    for (const MachinePlacementDefinition& placement :
         content.machine_placement_definitions()) {
        const TerrainMaterialDef* material = worldgen.find_material(placement.material_key);
        const bool collides_with_block = std::any_of(
            config.block_definitions.begin(), config.block_definitions.end(),
            [&placement](const GameServerBlockDefinition& block) {
                return block.material_key == placement.material_key;
            });
        if (material == nullptr || material->id == worldgen.roles.air ||
            material->id == config.reserved_grave_material_id ||
            collides_with_block) {
            return invalid_argument("Machine placement item '" + placement.item_id +
                                    "' has a terrain material that conflicts with the "
                                    "server interaction catalog");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_automation_controller_placement_catalog(
    const GameContentRegistry& content,
    const GameServerPlayerInteractionConfig& config) {
    if (auto result = content.validate_automation_controller_placement_references(); !result) {
        return result.error();
    }
    const WorldGenConfigSnapshot& worldgen = *config.worldgen_config;
    std::set<std::string, std::less<>> machine_materials;
    for (const MachinePlacementDefinition& placement : content.machine_placement_definitions()) {
        machine_materials.insert(placement.material_key);
    }
    for (const AutomationControllerPlacementDefinition& placement :
         content.automation_controller_placement_definitions()) {
        const TerrainMaterialDef* material = worldgen.find_material(placement.material_key);
        const bool collides_with_block = std::any_of(
            config.block_definitions.begin(), config.block_definitions.end(),
            [&placement](const GameServerBlockDefinition& block) {
                return block.material_key == placement.material_key;
            });
        if (material == nullptr || material->id == worldgen.roles.air ||
            material->id == config.reserved_grave_material_id || collides_with_block ||
            machine_materials.contains(placement.material_key)) {
            return invalid_argument("Automation controller placement item '" + placement.item_id +
                                    "' has a terrain material that conflicts with the "
                                    "server interaction catalog");
        }
    }
    return {};
}

enum class MiningDropPolicy : uint8_t {
    kGrantDrops,
    kBreakWithoutDrops,
};

[[nodiscard]] snt::core::Expected<MiningDropPolicy> mining_drop_policy(
    const GameContentRegistry& content, GameServerPlayerState& player_state,
    const GameAuthenticatedPeer& peer, const TerrainMaterialDef& material) {
    auto main_hand_item_id = player_state.main_hand_item_id_for_peer(peer);
    if (!main_hand_item_id) return main_hand_item_id.error();

    const std::vector<std::string> tool_tags = content.tool_tags_for_item(*main_hand_item_id);
    const bool tag_matches = material.required_tool_tag.empty() ||
        std::find(tool_tags.begin(), tool_tags.end(), material.required_tool_tag) !=
            tool_tags.end();
    int32_t mining_level = 0;
    if (const GameItemDefinition* item = content.find_item(*main_hand_item_id);
        item != nullptr && item->tool.has_value()) {
        mining_level = item->tool->mining_level;
    }

    // A matching but under-level tool cannot mine the block at all. A wrong
    // tool may break a levelled block but never receives its material drops,
    // matching the retired command-server behavior.
    if (tag_matches && mining_level < material.required_mining_level) {
        return invalid_state("Held tool mining level is below the terrain material requirement");
    }
    if (!tag_matches && material.required_mining_level > 0) {
        return MiningDropPolicy::kBreakWithoutDrops;
    }
    return MiningDropPolicy::kGrantDrops;
}

void hash_text(uint64_t& value, std::string_view text) noexcept {
    for (const unsigned char byte : text) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
}

void hash_u64(uint64_t& value, uint64_t input) noexcept {
    for (int shift = 0; shift < 64; shift += 8) {
        value ^= (input >> shift) & 0xffU;
        value *= 1099511628211ULL;
    }
}

[[nodiscard]] uint64_t deterministic_drop_roll(
    const GameBlockInteractionCommand& command, uint64_t tick_index,
    std::string_view material_key, size_t drop_index, uint64_t salt) noexcept {
    uint64_t value = 1469598103934665603ULL;
    hash_text(value, command.dimension_id);
    hash_u64(value, static_cast<uint32_t>(command.block_x));
    hash_u64(value, static_cast<uint32_t>(command.block_y));
    hash_u64(value, static_cast<uint32_t>(command.block_z));
    hash_u64(value, tick_index);
    hash_text(value, material_key);
    hash_u64(value, drop_index);
    hash_u64(value, salt);
    return value;
}

[[nodiscard]] snt::core::Expected<std::vector<GamePlayerItemStack>> make_mining_drops(
    const TerrainMaterialDef& material, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    std::vector<GamePlayerItemStack> additions;
    additions.reserve(material.drops.size());
    for (size_t drop_index = 0; drop_index < material.drops.size(); ++drop_index) {
        const TerrainDropDef& drop = material.drops[drop_index];
        const uint64_t chance_roll = deterministic_drop_roll(
            command, tick_index, material.key, drop_index, 0x63b5d167ULL);
        const double chance_sample = static_cast<double>(chance_roll >> 11) /
            static_cast<double>(1ULL << 53);
        if (chance_sample >= static_cast<double>(drop.chance)) continue;
        const uint64_t amount_roll = deterministic_drop_roll(
            command, tick_index, material.key, drop_index, 0x1db50b19ULL);
        const uint64_t range = static_cast<uint64_t>(drop.max_count - drop.min_count) + 1u;
        const int32_t count = drop.min_count + static_cast<int32_t>(amount_roll % range);
        auto existing = std::find_if(additions.begin(), additions.end(),
            [&drop](const GamePlayerItemStack& stack) {
                return stack.resource.key == ResourceContentKey::item(drop.item_key);
            });
        if (existing == additions.end()) {
            additions.push_back(GamePlayerItemStack::item(drop.item_key, count));
        } else {
            existing->resource.amount += count;
        }
    }
    if (additions.size() > kMaxCollectedMachineStacks) {
        return invalid_state("Terrain material produces too many distinct inventory stacks");
    }
    return additions;
}

[[nodiscard]] snt::core::Expected<GamePlayerInventoryTransaction>
make_crop_harvest_transaction(const CropHarvestResult& harvest) {
    GamePlayerInventoryTransaction transaction;
    const auto append = [&transaction](std::string_view item_id, int32_t count,
                                       std::string_view source) -> snt::core::Expected<void> {
        if (count <= 0) return {};
        if (item_id.empty() || item_id.size() > kMaxGameItemIdBytes) {
            return invalid_state("Crop harvest has an invalid " + std::string(source) +
                                 " item key");
        }
        transaction.additions.push_back(GamePlayerItemStack::item(std::string(item_id), count));
        return {};
    };
    if (auto result = append(harvest.crop_item_key, harvest.crop_count, "crop"); !result) {
        return result.error();
    }
    if (auto result = append(harvest.byproduct_item_key, harvest.byproduct_count,
                             "byproduct");
        !result) {
        return result.error();
    }
    return transaction;
}

}  // namespace

std::vector<GameServerBlockDefinition>
GameServerPlayerInteractionService::default_block_definitions() {
    return {
        {.item_id = "stone", .material_key = "snt:stone"},
        {.item_id = "dirt", .material_key = "snt:dirt"},
        {.item_id = "sand", .material_key = "snt:sand"},
        {.item_id = "snow", .material_key = "snt:snow"},
        {.item_id = "bed", .material_key = "snt:runtime.bed", .is_bed = true},
        {.item_id = "fire", .material_key = "snt:runtime.fire"},
    };
}

snt::core::Expected<std::unique_ptr<GameServerPlayerInteractionService>>
GameServerPlayerInteractionService::create(
    snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
    GameServerPlayerBedService& beds, const GameContentRegistry& content,
    MachineInteractionService& machine_interactions,
    GameServerInventoryReplication* inventory_replication,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    std::vector<IGameServerPlayerInteractionEventSink*> event_sinks,
    GameServerPlayerInteractionConfig config) {
    if (config.block_definitions.empty()) {
        config.block_definitions = default_block_definitions();
    }
    if (auto result = validate_catalog(config); !result) return result.error();
    if (auto result = validate_machine_placement_catalog(content, config); !result) {
        return result.error();
    }
    if (auto result = validate_automation_controller_placement_catalog(content, config); !result) {
        return result.error();
    }
    if (std::any_of(event_sinks.begin(), event_sinks.end(),
                    [](const IGameServerPlayerInteractionEventSink* sink) {
                        return sink == nullptr;
                    })) {
        return invalid_argument("Game server player interaction has a null event sink");
    }
    return std::unique_ptr<GameServerPlayerInteractionService>(
        new GameServerPlayerInteractionService(
            world, chunks, sidecars, player_state, beds, content, machine_interactions,
            inventory_replication, checkpoint_sink, std::move(event_sinks), std::move(config)));
}

GameServerPlayerInteractionService::GameServerPlayerInteractionService(
    snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
    GameServerPlayerBedService& beds, const GameContentRegistry& content,
    MachineInteractionService& machine_interactions,
    GameServerInventoryReplication* inventory_replication,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    std::vector<IGameServerPlayerInteractionEventSink*> event_sinks,
    GameServerPlayerInteractionConfig config)
    : world_(&world), chunks_(&chunks), sidecars_(&sidecars), player_state_(&player_state),
      beds_(&beds), content_(&content), machine_interactions_(&machine_interactions),
      inventory_replication_(inventory_replication), checkpoint_sink_(checkpoint_sink),
      event_sinks_(std::move(event_sinks)),
      config_(std::move(config)) {}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_block_interaction(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (world_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr ||
        player_state_ == nullptr || beds_ == nullptr || content_ == nullptr ||
        machine_interactions_ == nullptr) {
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
        case GameBlockInteractionAction::kTillFarmland:
            return apply_till_farmland(peer, command, tick_index);
        case GameBlockInteractionAction::kPlantCrop:
            return apply_plant_crop(peer, command, tick_index);
        case GameBlockInteractionAction::kFertilizeCrop:
            return apply_fertilize_crop(peer, command, tick_index);
        case GameBlockInteractionAction::kHarvestCrop:
            return apply_harvest_crop(peer, command, tick_index);
    }
    return invalid_argument("Game server player interaction action is invalid");
}

snt::core::Expected<void>
GameServerPlayerInteractionService::submit_machine_input_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GameMachineInputSlotTransferCommand& command,
    uint64_t tick_index) {
    if (world_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr ||
        player_state_ == nullptr || inventory_replication_ == nullptr) {
        return invalid_state("Game server machine input transfer service is unavailable");
    }
    if (auto result = validate_game_machine_input_slot_transfer_command(command); !result) {
        return result.error();
    }
    auto revision_matches = inventory_replication_->matches_inventory_revision(
        peer, command.expected_inventory_revision);
    if (!revision_matches) return revision_matches.error();
    if (!*revision_matches) {
        return inventory_replication_->record_command_response(
            peer, GameInventoryCommandKind::kMachineInputSlotTransfer,
            command.request_id, GameInventorySlotTransferOutcome::kRejected,
            "inventory revision is stale");
    }

    auto applied = apply_machine_input_slot_transfer(peer, command, tick_index);
    if (!applied) {
        return inventory_replication_->record_command_response(
            peer, GameInventoryCommandKind::kMachineInputSlotTransfer,
            command.request_id, GameInventorySlotTransferOutcome::kRejected,
            applied.error().message());
    }
    return inventory_replication_->record_command_response(
        peer, GameInventoryCommandKind::kMachineInputSlotTransfer,
        command.request_id, GameInventorySlotTransferOutcome::kAccepted);
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_mine(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    if (config_.worldgen_config == nullptr) {
        return invalid_state("Game server interaction has no worldgen snapshot");
    }
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    const uint32_t material = static_cast<uint32_t>(target->cell->material);
    const TerrainMaterialDef* terrain_material = worldgen.find_material(
        static_cast<TerrainMaterialId>(material));
    if (terrain_material == nullptr || material == worldgen.roles.air ||
        (terrain_material->flags & static_cast<uint32_t>(snt::voxel::TF_MINEABLE)) == 0 ||
        (terrain_material->flags & static_cast<uint32_t>(snt::voxel::TF_INDESTRUCTIBLE)) != 0) {
        return invalid_state("Client mining target is not host-mineable");
    }
    const GameServerBlockDefinition* definition = find_block_by_material(material);
    GameChunkSidecar* sidecar = sidecars_->get(target->chunk_key);
    if (sidecar != nullptr && has_non_bed_sidecar_claim(*sidecar, target->position)) {
        return invalid_state("Client mining target owns a protected game sidecar anchor");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    const bool is_bed = definition != nullptr && definition->is_bed;
    if (is_bed != *bed_present) {
        return invalid_state("Client mining target has inconsistent bed sidecar state");
    }

    auto drop_policy = mining_drop_policy(*content_, *player_state_, peer, *terrain_material);
    if (!drop_policy) return drop_policy.error();
    std::vector<GamePlayerItemStack> additions;
    if (*drop_policy == MiningDropPolicy::kGrantDrops) {
        auto resolved_drops = make_mining_drops(*terrain_material, command, tick_index);
        if (!resolved_drops) return resolved_drops.error();
        additions = std::move(*resolved_drops);
    }
    const GamePlayerInventoryTransaction transaction{.additions = additions};
    if (!transaction.additions.empty()) {
        auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
        if (!can_apply) return can_apply.error();
        if (!*can_apply) {
            return invalid_state("Client mining result does not fit the host inventory");
        }
    }
    if (is_bed || !transaction.additions.empty()) {
        if (auto result = mark_player_state_dirty(peer); !result) return result.error();
    }

    bool removed_bed = false;
    if (is_bed) {
        if (auto result = beds_->on_bed_removed(target->position); !result) return result.error();
        removed_bed = true;
    }
    const snt::voxel::TerrainCell previous = *target->cell;
    const TerrainMaterialDef* air_material = worldgen.find_material(worldgen.roles.air);
    *target->cell = {
        .material = static_cast<snt::voxel::TerrainMaterial>(worldgen.roles.air),
        .flags = air_material == nullptr ? 0u : air_material->flags,
    };
    if (!transaction.additions.empty()) {
        if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
            *target->cell = previous;
            if (removed_bed) {
                static_cast<void>(beds_->on_bed_placed(target->position));
            }
            auto error = result.error();
            error.with_context("GameServerPlayerInteractionService::apply_mine(inventory commit)");
            return error;
        }
    }
    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBlockMined,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = !transaction.additions.empty()
            ? transaction.additions.front().resource.key.id :
            (definition != nullptr ? definition->item_id : terrain_material->key),
        .previous_material = material,
        .current_material = worldgen.roles.air,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_place(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (content_->find_automation_controller_placement_by_item(command.selected_item_id) != nullptr) {
        return apply_automation_controller_place(peer, command, tick_index);
    }
    if (content_->find_machine_placement_by_item(command.selected_item_id) != nullptr) {
        return apply_machine_place(peer, command, tick_index);
    }
    if (config_.worldgen_config == nullptr) {
        return invalid_state("Game server interaction has no worldgen snapshot");
    }
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    if (static_cast<uint32_t>(target->cell->material) != worldgen.roles.air ||
        target->cell->has_fluid()) {
        return invalid_state("Client placement target is not an empty host terrain cell");
    }
    const GameServerBlockDefinition* definition = find_block_by_item(command.selected_item_id);
    if (definition == nullptr) {
        return invalid_state("Client placement item is not in the host block catalog");
    }
    const TerrainMaterialDef* terrain_material = find_terrain_material(definition->material_key);
    if (terrain_material == nullptr) {
        return invalid_state("Client placement refers to an unavailable terrain material key");
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
        .removals = {GamePlayerItemStack::item(definition->item_id, 1)},
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
        .material = static_cast<snt::voxel::TerrainMaterial>(terrain_material->id),
        .flags = terrain_material->flags,
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
    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBlockPlaced,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = definition->item_id,
        .previous_material = worldgen.roles.air,
        .current_material = terrain_material->id,
    });
    return {};
}

snt::core::Expected<void>
GameServerPlayerInteractionService::apply_automation_controller_place(
    const GameAuthenticatedPeer& peer,
    const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.worldgen_config == nullptr) {
        return invalid_state("Game server interaction has no worldgen snapshot");
    }
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    if (static_cast<uint32_t>(target->cell->material) != worldgen.roles.air ||
        target->cell->has_fluid()) {
        return invalid_state(
            "Client automation controller placement target is not an empty host terrain cell");
    }

    const AutomationControllerPlacementDefinition* placement =
        content_->find_automation_controller_placement_by_item(command.selected_item_id);
    if (placement == nullptr) {
        return invalid_state(
            "Client automation controller item is not in the host placement registry");
    }
    const TerrainMaterialDef* terrain_material = find_terrain_material(placement->material_key);
    if (terrain_material == nullptr || terrain_material->id == worldgen.roles.air ||
        terrain_material->id == config_.reserved_grave_material_id ||
        find_block_by_material(terrain_material->id) != nullptr ||
        content_->find_machine_placement_by_material_key(placement->material_key) != nullptr) {
        return invalid_state("Automation controller placement material conflicts with the host catalog");
    }

    GameChunkSidecar* sidecar = sidecars_->get(target->chunk_key);
    if (sidecar != nullptr && has_non_bed_sidecar_claim(*sidecar, target->position)) {
        return invalid_state("Client automation controller placement target owns a protected game sidecar anchor");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    if (*bed_present) {
        return invalid_state("Client automation controller placement target already owns a player bed anchor");
    }

    const GamePlayerInventoryTransaction transaction{
        .removals = {GamePlayerItemStack::item(placement->item_id, 1)},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Client automation controller item is absent from the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();

    const bool created_sidecar = sidecar == nullptr;
    if (created_sidecar) sidecars_->set(target->chunk_key, {});
    auto anchor = GameAutomationControllerPersistence::create_controller(
        *sidecars_, target->chunk_key,
        target->position.position.x, target->position.position.y, target->position.position.z,
        {.kind = placement->kind,
         .controller_key = placement->controller_key,
         .sfm_program = {}});
    if (!anchor) {
        if (created_sidecar) sidecars_->remove(target->chunk_key);
        return anchor.error();
    }

    // resolve_target() only succeeds for a resident chunk. Materialize the
    // new owner before consuming the item so active AOI/UI readers cannot see
    // an accepted controller block without its SFM or AE runtime state.
    if (auto result = refresh_automation_runtime(target->chunk_key); !result) {
        const auto refresh_error = result.error();
        if (auto rollback = GameAutomationControllerPersistence::remove_controller(
                *sidecars_, anchor->anchor_entity_id);
            !rollback) {
            SNT_LOG_ERROR(
                "Automation controller runtime refresh rollback left an anchor for '%s' at (%d,%d,%d): %s",
                placement->controller_key.c_str(), target->position.position.x,
                target->position.position.y, target->position.position.z,
                rollback.error().format().c_str());
        } else {
            if (created_sidecar) sidecars_->remove(target->chunk_key);
            if (auto restored = refresh_automation_runtime(target->chunk_key); !restored) {
                SNT_LOG_ERROR(
                    "Automation controller runtime rollback refresh failed for '%s' at (%d,%d,%d): %s",
                    placement->controller_key.c_str(), target->position.position.x,
                    target->position.position.y, target->position.position.z,
                    restored.error().format().c_str());
            }
        }
        auto error = refresh_error;
        error.with_context(
            "GameServerPlayerInteractionService::apply_automation_controller_place(runtime refresh)");
        return error;
    }

    const snt::voxel::TerrainCell previous = *target->cell;
    *target->cell = {
        .material = static_cast<snt::voxel::TerrainMaterial>(terrain_material->id),
        .flags = terrain_material->flags,
    };
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        *target->cell = previous;
        if (auto rollback = GameAutomationControllerPersistence::remove_controller(
                *sidecars_, anchor->anchor_entity_id);
            !rollback) {
            SNT_LOG_ERROR(
                "Automation controller placement rollback left an anchor for '%s' at (%d,%d,%d): %s",
                placement->controller_key.c_str(), target->position.position.x,
                target->position.position.y, target->position.position.z,
                rollback.error().format().c_str());
        } else {
            if (created_sidecar) sidecars_->remove(target->chunk_key);
            if (auto restored = refresh_automation_runtime(target->chunk_key); !restored) {
                SNT_LOG_ERROR(
                    "Automation controller inventory rollback refresh failed for '%s' at (%d,%d,%d): %s",
                    placement->controller_key.c_str(), target->position.position.x,
                    target->position.position.y, target->position.position.z,
                    restored.error().format().c_str());
            }
        }
        auto error = result.error();
        error.with_context(
            "GameServerPlayerInteractionService::apply_automation_controller_place(inventory commit)");
        return error;
    }

    schedule_terrain_simulation_after_commit(command, tick_index);
    SNT_LOG_INFO(
        "Placed automation controller '%s' for account '%s' at (%d,%d,%d) anchor=%llu",
        placement->controller_key.c_str(), peer.identity.account_id.c_str(),
        target->position.position.x, target->position.position.y, target->position.position.z,
        static_cast<unsigned long long>(anchor->anchor_entity_id.id));
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kBlockPlaced,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = placement->item_id,
        .previous_material = worldgen.roles.air,
        .current_material = terrain_material->id,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_machine_place(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.worldgen_config == nullptr) {
        return invalid_state("Game server interaction has no worldgen snapshot");
    }
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    if (static_cast<uint32_t>(target->cell->material) != worldgen.roles.air ||
        target->cell->has_fluid()) {
        return invalid_state("Client machine placement target is not an empty host terrain cell");
    }

    const MachinePlacementDefinition* placement =
        content_->find_machine_placement_by_item(command.selected_item_id);
    if (placement == nullptr) {
        return invalid_state("Client machine placement item is not in the host placement registry");
    }
    const TerrainMaterialDef* terrain_material = find_terrain_material(placement->material_key);
    if (terrain_material == nullptr) {
        return invalid_state("Machine placement refers to an unavailable terrain material key");
    }
    const MachineDefinition* machine = content_->find_machine(placement->machine_id);
    if (machine == nullptr) {
        return invalid_state("Machine placement refers to a missing current machine definition");
    }
    if (terrain_material->id == worldgen.roles.air ||
        terrain_material->id == config_.reserved_grave_material_id ||
        find_block_by_material(terrain_material->id) != nullptr) {
        return invalid_state("Machine placement material conflicts with a host terrain material");
    }

    GameChunkSidecar* sidecar = sidecars_->get(target->chunk_key);
    if (sidecar != nullptr && has_non_bed_sidecar_claim(*sidecar, target->position)) {
        return invalid_state("Client machine placement target owns a protected game sidecar anchor");
    }
    auto bed_present = beds_->has_bed_at(target->position);
    if (!bed_present) return bed_present.error();
    if (*bed_present) {
        return invalid_state("Client machine placement target already owns a player bed anchor");
    }

    const GamePlayerInventoryTransaction transaction{
        .removals = {GamePlayerItemStack::item(placement->item_id, 1)},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Client machine placement item is absent from the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();

    const bool created_sidecar = sidecar == nullptr;
    if (created_sidecar) {
        sidecars_->set(target->chunk_key, {});
    }

    MachineRuntimeComponent runtime;
    runtime.machine_id = machine->id;
    runtime.resource_runtime_index = content_->resource_runtime_index();
    runtime.energy_capacity = machine->power_capacity;
    auto anchored = GameMachineRuntimePersistence::create_anchored_machine(
        *world_, *sidecars_, target->chunk_key, target->position.position.x,
        target->position.position.y, target->position.position.z, std::move(runtime));
    if (!anchored) {
        if (created_sidecar) sidecars_->remove(target->chunk_key);
        return anchored.error();
    }

    const snt::voxel::TerrainCell previous = *target->cell;
    *target->cell = {
        .material = static_cast<snt::voxel::TerrainMaterial>(terrain_material->id),
        .flags = terrain_material->flags,
    };
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        *target->cell = previous;
        if (auto rollback = GameMachineRuntimePersistence::remove_anchored_machine(
                *world_, *sidecars_, anchored->entity_guid);
            !rollback) {
            SNT_LOG_ERROR(
                "Machine placement rollback left an anchor for machine '%s' at (%d,%d,%d): %s",
                machine->id.c_str(), target->position.position.x, target->position.position.y,
                target->position.position.z, rollback.error().format().c_str());
        } else if (created_sidecar) {
            sidecars_->remove(target->chunk_key);
        }
        auto error = result.error();
        error.with_context("GameServerPlayerInteractionService::apply_machine_place(inventory commit)");
        return error;
    }

    schedule_terrain_simulation_after_commit(command, tick_index);
    SNT_LOG_INFO("Placed machine '%s' for account '%s' at (%d,%d,%d) anchor=%llu runtime=%llu",
                 machine->id.c_str(), peer.identity.account_id.c_str(),
                 target->position.position.x, target->position.position.y,
                 target->position.position.z,
                 static_cast<unsigned long long>(anchored->anchor_entity_id.id),
                 static_cast<unsigned long long>(anchored->entity_guid.value));
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kMachinePlaced,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = placement->item_id,
        .machine_id = machine->id,
        .previous_material = worldgen.roles.air,
        .current_material = terrain_material->id,
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

    auto main_hand_item_id = player_state_->main_hand_item_id_for_peer(peer);
    if (!main_hand_item_id) return main_hand_item_id.error();
    const std::vector<std::string> held_tags = content_->tool_tags_for_item(*main_hand_item_id);

    const MachineActivationContext context{
        .target_is_reachable = true,
        .cover_is_present =
            (command.client_hints & kGameBlockInteractionHintCover) != 0,
        .ignition_is_present =
            (command.client_hints & kGameBlockInteractionHintIgnition) != 0,
        .structure_is_valid =
            (command.client_hints & kGameBlockInteractionHintStructure) != 0,
        .held_tool_tags = held_tags,
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

snt::core::Expected<void>
GameServerPlayerInteractionService::apply_machine_input_slot_transfer(
    const GameAuthenticatedPeer& peer,
    const GameMachineInputSlotTransferCommand& command,
    uint64_t tick_index) {
    static_cast<void>(tick_index);
    const GameBlockInteractionCommand target_command{
        .action = GameBlockInteractionAction::kActivateMachine,
        .dimension_id = command.dimension_id,
        .block_x = command.root_x,
        .block_y = command.root_y,
        .block_z = command.root_z,
        .expected_material = command.expected_material,
    };
    auto target = resolve_target(*player_state_, *chunks_, peer, target_command);
    if (!target) return target.error();
    auto machine_guid = find_machine_at(*sidecars_, *target);
    if (!machine_guid) return machine_guid.error();
    const entt::entity entity = world_->find_entity_by_guid(*machine_guid);
    if (entity == entt::null ||
        !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
        return invalid_state("Machine input transfer target has no live host runtime");
    }
    MachineRuntimeComponent& runtime = world_->get_component<MachineRuntimeComponent>(entity);
    if (runtime.max_input_slots <= 0 ||
        runtime.max_input_slots > static_cast<int32_t>(kMaxGameMachineInputSlots) ||
        runtime.max_stack_size <= 0 || runtime.max_stack_size > kMaxGameInventoryStackSize ||
        runtime.input_slots.size() > static_cast<size_t>(runtime.max_input_slots)) {
        return invalid_state("Machine input transfer target has an invalid input layout");
    }

    auto player_inventory = player_state_->inventory_for_peer(peer);
    if (!player_inventory) return player_inventory.error();
    if (command.player_slot >= player_inventory->slots.size()) {
        return invalid_state("Machine input transfer player slot is outside the host inventory");
    }
    if (player_inventory->slots[command.player_slot] != command.expected_player_slot) {
        return invalid_state("Machine input transfer player slot changed before host commit");
    }

    const ResourceRuntimeIndex::Snapshot resource_runtime_index =
        runtime.resource_runtime_index;
    if (!resource_runtime_index.key_context().is_valid() ||
        !resource_runtime_index.key_context().matches(
            content_->resource_runtime_index().key_context())) {
        return invalid_state("Machine input transfer target has a stale resource snapshot");
    }
    const auto to_content_stack = [&resource_runtime_index](const ResourceStack& stack)
        -> snt::core::Expected<ResourceContentStack> {
        const auto content = resolve_content_stack(stack, resource_runtime_index);
        if (!content || !content->is_valid()) {
            return invalid_state("Machine input transfer could not resolve a compact resource stack");
        }
        return *content;
    };
    const auto to_runtime_stack = [&resource_runtime_index](const GamePlayerItemStack& stack)
        -> snt::core::Expected<ResourceStack> {
        if (!stack.is_valid_item() || !stack.instance_data.empty()) {
            return invalid_state("Machine input transfer cannot store a singular player item");
        }
        const auto runtime_stack = resolve_resource_stack(
            stack.resource, resource_runtime_index);
        if (!runtime_stack || !runtime_stack->is_valid()) {
            return invalid_state("Machine input transfer player resource is unavailable in current content");
        }
        return *runtime_stack;
    };

    std::vector<ResourceStack> candidate_inputs = runtime.input_slots;
    for (const ResourceStack& input : candidate_inputs) {
        auto content = to_content_stack(input);
        if (!content || !content->is_item() ||
            content->key.id.size() > kMaxGameItemIdBytes ||
            input.amount > runtime.max_stack_size) {
            return invalid_state("Machine input transfer target has invalid input storage");
        }
    }
    const size_t machine_slot_index = command.machine_input_slot;
    if (machine_slot_index > candidate_inputs.size()) {
        return invalid_state("Machine input transfer references an unavailable input position");
    }
    if (machine_slot_index == candidate_inputs.size()) {
        if (candidate_inputs.size() >= static_cast<size_t>(runtime.max_input_slots)) {
            return invalid_state("Machine input transfer has no free input position");
        }
        candidate_inputs.emplace_back();
    }

    GamePlayerItemStack candidate_player = player_inventory->slots[command.player_slot];
    ResourceStack& candidate_machine = candidate_inputs[machine_slot_index];
    GamePlayerItemStack observed_machine;
    if (!candidate_machine.is_empty()) {
        auto content = to_content_stack(candidate_machine);
        if (!content) return content.error();
        observed_machine.resource = std::move(*content);
    }
    if (observed_machine != command.expected_machine_input_slot) {
        return invalid_state("Machine input transfer input position changed before host commit");
    }

    const auto clear_player = [](GamePlayerItemStack& stack) {
        stack.clear();
    };

    if (command.direction ==
        GameMachineInputSlotTransferDirection::kPlayerToMachineInput) {
        auto player_runtime = to_runtime_stack(candidate_player);
        if (!player_runtime) return player_runtime.error();
        if (candidate_machine.is_empty()) {
            ResourceStack moved = *player_runtime;
            moved.amount = command.count;
            candidate_machine = std::move(moved);
            candidate_player.resource.amount -= command.count;
            if (candidate_player.resource.amount == 0) clear_player(candidate_player);
        } else if (candidate_machine.key == player_runtime->key) {
            if (candidate_machine.amount > runtime.max_stack_size - command.count) {
                return invalid_state("Machine input transfer does not fit the target stack");
            }
            candidate_machine.amount += command.count;
            candidate_player.resource.amount -= command.count;
            if (candidate_player.resource.amount == 0) clear_player(candidate_player);
        } else {
            if (command.count != candidate_player.resource.amount) {
                return invalid_state("Machine input transfer can only swap a complete player stack");
            }
            if (candidate_machine.amount > player_inventory->max_stack_size) {
                return invalid_state("Machine input transfer target stack exceeds player capacity");
            }
            auto previous_machine = to_content_stack(candidate_machine);
            if (!previous_machine) return previous_machine.error();
            candidate_machine = *player_runtime;
            candidate_player.resource = std::move(*previous_machine);
            candidate_player.instance_data.clear();
        }
    } else {
        if (candidate_machine.is_empty()) {
            return invalid_state("Machine input transfer source slot is empty");
        }
        auto machine_content = to_content_stack(candidate_machine);
        if (!machine_content) return machine_content.error();
        if (candidate_player.is_empty()) {
            GamePlayerItemStack moved;
            moved.resource = *machine_content;
            moved.resource.amount = command.count;
            candidate_player = std::move(moved);
            candidate_machine.amount -= command.count;
            if (candidate_machine.amount == 0) candidate_machine = {};
        } else if (candidate_player.instance_data.empty() &&
                   candidate_player.resource.has_same_key(*machine_content)) {
            if (candidate_player.resource.amount > player_inventory->max_stack_size - command.count) {
                return invalid_state("Machine input transfer does not fit the player stack");
            }
            candidate_player.resource.amount += command.count;
            candidate_machine.amount -= command.count;
            if (candidate_machine.amount == 0) candidate_machine = {};
        } else {
            if (command.count != candidate_machine.amount) {
                return invalid_state("Machine input transfer can only swap a complete machine stack");
            }
            if (!candidate_player.instance_data.empty() ||
                candidate_player.resource.amount > runtime.max_stack_size) {
                return invalid_state("Machine input transfer player stack cannot enter the machine");
            }
            auto previous_player = to_runtime_stack(candidate_player);
            if (!previous_player) return previous_player.error();
            candidate_player.resource = std::move(*machine_content);
            candidate_player.instance_data.clear();
            candidate_machine = std::move(*previous_player);
        }
    }

    std::erase_if(candidate_inputs, [](const ResourceStack& stack) {
        return stack.is_empty();
    });
    const GamePlayerInventorySlotMutation mutation{
        .slot = command.player_slot,
        .expected = command.expected_player_slot,
        .replacement = candidate_player,
    };
    auto applicable = player_state_->can_apply_inventory_slot_mutations(peer, {&mutation, 1});
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return invalid_state("Machine input transfer player slot changed before host commit");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();
    if (auto result = player_state_->apply_inventory_slot_mutations(peer, {&mutation, 1});
        !result) {
        return result.error();
    }
    runtime.input_slots = std::move(candidate_inputs);
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
    if (!runtime.resource_runtime_index.key_context().is_valid() ||
        !runtime.resource_runtime_index.key_context().matches(
            content_->resource_runtime_index().key_context())) {
        return invalid_state("Machine collect target has a stale resource snapshot");
    }
    GamePlayerInventoryTransaction transaction;
    transaction.additions.reserve(runtime.output_slots.size());
    for (const ResourceStack& output : runtime.output_slots) {
        if (output.is_empty()) continue;
        const auto content = resolve_content_stack(output, runtime.resource_runtime_index);
        if (!content || !content->is_item() ||
            content->key.id.size() > kMaxGameItemIdBytes ||
            transaction.additions.size() == kMaxCollectedMachineStacks) {
            return invalid_state("Machine collect target has invalid output storage");
        }
        transaction.additions.push_back({.resource = *content});
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
    runtime.output_slots.clear();
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kMachineOutputCollected,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_till_farmland(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.crop_growth_system == nullptr || config_.worldgen_config == nullptr) {
        return invalid_state("Game server farmland interaction has no crop growth authority");
    }
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    const TerrainMaterialDef* terrain = worldgen.find_material(
        static_cast<TerrainMaterialId>(target->cell->material));
    if (terrain == nullptr || target->cell->material != worldgen.roles.dirt ||
        terrain->required_tool_tag.empty() ||
        !content_->item_matches_tool_requirement(command.selected_item_id,
                                                  terrain->required_tool_tag,
                                                  terrain->required_mining_level)) {
        return invalid_state("Farmland tilling requires the configured dirt tool");
    }
    if (auto result = validate_selected_inventory_item(peer, command.selected_item_id); !result) {
        return result.error();
    }

    const uint32_t previous_material = static_cast<uint32_t>(target->cell->material);
    auto tilled = config_.crop_growth_system->till_farmland({
        .dimension_id = command.dimension_id,
        .block_x = command.block_x,
        .block_y = command.block_y,
        .block_z = command.block_z,
    }, tick_index);
    if (!tilled) return tilled.error();

    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kFarmlandTilled,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = command.selected_item_id,
        .previous_material = previous_material,
        .current_material = worldgen.runtime_ids.farmland,
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_plant_crop(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.crop_growth_system == nullptr || config_.worldgen_config == nullptr) {
        return invalid_state("Game server crop planting has no crop growth authority");
    }
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const WorldGenConfigSnapshot& worldgen = *config_.worldgen_config;
    const CropSpeciesDef* species = worldgen.find_crop_by_seed(command.selected_item_id);
    if (species == nullptr || content_->find_item(command.selected_item_id) == nullptr) {
        return invalid_state("Crop planting selected item is not a registered seed");
    }
    if (target->cell->material != worldgen.roles.air || target->cell->has_fluid()) {
        return invalid_state("Crop planting target is not an empty host terrain cell");
    }

    const GamePlayerInventoryTransaction transaction{
        .removals = {GamePlayerItemStack::item(command.selected_item_id, 1)},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) return invalid_state("Crop planting seed is absent from the host inventory");
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        return result.error();
    }

    auto planted = config_.crop_growth_system->plant_crop({
        .dimension_id = command.dimension_id,
        .block_x = command.block_x,
        .block_y = command.block_y,
        .block_z = command.block_z,
        .species_key = species->species_key,
    }, tick_index);
    if (!planted) {
        rollback_inventory_transaction(peer, transaction, "crop planting");
        return planted.error();
    }

    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kCropPlanted,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = command.selected_item_id,
        .previous_material = worldgen.roles.air,
        .current_material = worldgen.material_id_or(
            species->stage_material_keys[static_cast<size_t>(CropGrowthStage::SEED)],
            worldgen.roles.air),
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_fertilize_crop(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.crop_growth_system == nullptr || config_.worldgen_config == nullptr) {
        return invalid_state("Game server crop fertilization has no crop growth authority");
    }
    if (config_.fertilizer_item_id.empty() ||
        command.selected_item_id != config_.fertilizer_item_id ||
        content_->find_item(command.selected_item_id) == nullptr) {
        return invalid_state("Crop fertilization requires the configured fertilizer item");
    }
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const uint32_t previous_material = static_cast<uint32_t>(target->cell->material);
    const GamePlayerInventoryTransaction transaction{
        .removals = {GamePlayerItemStack::item(command.selected_item_id, 1)},
    };
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Crop fertilization item is absent from the host inventory");
    }
    if (auto result = mark_player_state_dirty(peer); !result) return result.error();
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        return result.error();
    }

    auto stage = config_.crop_growth_system->fertilize_crop({
        .dimension_id = command.dimension_id,
        .block_x = command.block_x,
        .block_y = command.block_y,
        .block_z = command.block_z,
    }, tick_index);
    if (!stage) {
        rollback_inventory_transaction(peer, transaction, "crop fertilization");
        return stage.error();
    }

    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kCropFertilized,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = command.selected_item_id,
        .previous_material = previous_material,
        .current_material = static_cast<uint32_t>(target->cell->material),
    });
    return {};
}

snt::core::Expected<void> GameServerPlayerInteractionService::apply_harvest_crop(
    const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
    uint64_t tick_index) {
    if (config_.crop_growth_system == nullptr) {
        return invalid_state("Game server crop harvest has no crop growth authority");
    }
    auto target = resolve_target(*player_state_, *chunks_, peer, command);
    if (!target) return target.error();
    const uint32_t previous_material = static_cast<uint32_t>(target->cell->material);
    const CropHarvestRequest request{
        .dimension_id = command.dimension_id,
        .block_x = command.block_x,
        .block_y = command.block_y,
        .block_z = command.block_z,
    };
    auto preview = config_.crop_growth_system->preview_harvest_crop(request, tick_index);
    if (!preview) return preview.error();
    auto transaction = make_crop_harvest_transaction(*preview);
    if (!transaction) return transaction.error();
    for (const GamePlayerItemStack& addition : transaction->additions) {
        if (content_->find_item(addition.resource.key.id) == nullptr) {
            return invalid_state("Crop harvest output is not registered in current item content");
        }
    }
    auto can_apply = player_state_->can_apply_inventory_transaction(peer, *transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) return invalid_state("Crop harvest output does not fit the host inventory");
    if (!transaction->additions.empty()) {
        if (auto result = mark_player_state_dirty(peer); !result) return result.error();
        if (auto result = player_state_->apply_inventory_transaction(peer, *transaction); !result) {
            return result.error();
        }
    }

    auto harvested = config_.crop_growth_system->harvest_crop(request, tick_index);
    if (!harvested) {
        if (!transaction->additions.empty()) {
            rollback_inventory_transaction(peer, *transaction, "crop harvest");
        }
        return harvested.error();
    }

    schedule_terrain_simulation_after_commit(command, tick_index);
    emit_event({
        .kind = GameServerPlayerInteractionEventKind::kCropHarvested,
        .account_id = peer.identity.account_id,
        .tick_index = tick_index,
        .command = command,
        .item_id = harvested->crop_item_key,
        .previous_material = previous_material,
        .current_material = static_cast<uint32_t>(target->cell->material),
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
    if (config_.worldgen_config == nullptr) return nullptr;
    const auto found = std::find_if(
        config_.block_definitions.begin(), config_.block_definitions.end(),
        [this, material_id](const GameServerBlockDefinition& definition) {
            const TerrainMaterialDef* material = find_terrain_material(definition.material_key);
            return material != nullptr && material->id == material_id;
        });
    return found == config_.block_definitions.end() ? nullptr : &*found;
}

const TerrainMaterialDef* GameServerPlayerInteractionService::find_terrain_material(
    std::string_view material_key) const noexcept {
    return config_.worldgen_config == nullptr
        ? nullptr
        : config_.worldgen_config->find_material(std::string(material_key));
}

snt::core::Expected<void> GameServerPlayerInteractionService::mark_player_state_dirty(
    const GameAuthenticatedPeer& peer) {
    if (checkpoint_sink_ == nullptr) return {};
    return checkpoint_sink_->mark_player_state_dirty(peer);
}

snt::core::Expected<void>
GameServerPlayerInteractionService::refresh_automation_runtime(const ChunkKey& chunk_key) {
    if (sidecars_ == nullptr) {
        return invalid_state("Automation runtime refresh has no game sidecar registry");
    }
    const GameChunkSidecar* const sidecar = sidecars_->get(chunk_key);
    if (config_.automation_controller_runtime != nullptr) {
        if (sidecar == nullptr) {
            config_.automation_controller_runtime->dematerialize_chunk(chunk_key);
        } else if (auto result = config_.automation_controller_runtime->materialize_chunk(
                       chunk_key, *sidecar);
                   !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerInteractionService::refresh_automation_runtime(SFM)");
            return error;
        }
    }
    if (config_.ae_network_runtime != nullptr) {
        if (sidecar == nullptr) {
            auto result = config_.ae_network_runtime->dematerialize_chunk(chunk_key);
            if (!result) {
                auto error = result.error();
                error.with_context(
                    "GameServerPlayerInteractionService::refresh_automation_runtime(AE remove)");
                return error;
            }
        } else if (auto result = config_.ae_network_runtime->materialize_chunk(chunk_key, *sidecar);
                   !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerInteractionService::refresh_automation_runtime(AE)");
            return error;
        }
    }
    return {};
}

snt::core::Expected<void>
GameServerPlayerInteractionService::validate_selected_inventory_item(
    const GameAuthenticatedPeer& peer, std::string_view item_id) const {
    if (item_id.empty()) return invalid_argument("Selected farming item id is empty");
    const GamePlayerInventoryTransaction transaction{
        .removals = {GamePlayerItemStack::item(std::string(item_id), 1)},
    };
    auto available = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!available) return available.error();
    if (!*available) {
        return invalid_state("Selected farming item is absent from the host inventory");
    }
    return {};
}

void GameServerPlayerInteractionService::rollback_inventory_transaction(
    const GameAuthenticatedPeer& peer, const GamePlayerInventoryTransaction& transaction,
    std::string_view operation) const noexcept {
    if (player_state_ == nullptr) return;
    const GamePlayerInventoryTransaction inverse{
        .removals = transaction.additions,
        .additions = transaction.removals,
    };
    if (auto result = player_state_->apply_inventory_transaction(peer, inverse); !result) {
        SNT_LOG_ERROR("Inventory rollback failed after %.*s world mutation rejection for account '%s': %s",
                      static_cast<int>(operation.size()), operation.data(),
                      peer.identity.account_id.c_str(), result.error().format().c_str());
    }
}

void GameServerPlayerInteractionService::schedule_terrain_simulation_after_commit(
    const GameBlockInteractionCommand& command, uint64_t tick_index) const {
    if (config_.block_physics_trigger != nullptr) {
        config_.block_physics_trigger->schedule_block_physics_after_terrain_mutation(
            command.dimension_id, command.block_x, command.block_y, command.block_z, tick_index);
    }
    if (config_.fluid_trigger != nullptr) {
        config_.fluid_trigger->schedule_fluid_after_terrain_mutation(
            command.dimension_id, command.block_x, command.block_y, command.block_z, tick_index);
    }
}

void GameServerPlayerInteractionService::emit_event(
    GameServerPlayerInteractionEvent event) const {
    for (IGameServerPlayerInteractionEventSink* sink : event_sinks_) {
        sink->on_player_interaction(event);
    }
}

}  // namespace snt::game::replication
