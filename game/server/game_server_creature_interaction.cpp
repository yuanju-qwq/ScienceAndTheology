// Dedicated-server native creature interaction implementation.

#define SNT_LOG_CHANNEL "game.server_creature_interaction"
#include "game/server/game_server_creature_interaction.h"

#include "core/error.h"
#include "core/log.h"
#include "game/client/game_content_registry.h"
#include "game/server/game_server_player_state.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr uint32_t kMaxTerrainPenSearchRadiusBlocks = 64;
constexpr uint32_t kMaxTerrainPenCells = 16384;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    const int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] int32_t local_coordinate(int32_t value, int32_t divisor) noexcept {
    return value - floor_divide(value, divisor) * divisor;
}

[[nodiscard]] snt::core::Expected<int32_t> floor_to_block(float value) {
    if (!std::isfinite(value) ||
        value < static_cast<float>(std::numeric_limits<int32_t>::min() + 1) ||
        value > static_cast<float>(std::numeric_limits<int32_t>::max() - 1)) {
        return invalid_argument("Creature position cannot be represented as a terrain block");
    }
    return static_cast<int32_t>(std::floor(value));
}

[[nodiscard]] snt::core::Expected<const snt::voxel::TerrainCell*> terrain_cell_at(
    snt::voxel::ChunkRegistry& chunks, std::string_view dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int32_t chunk_x = floor_divide(block_x, kChunkSize);
    const int32_t chunk_y = floor_divide(block_y, kChunkSize);
    const int32_t chunk_z = floor_divide(block_z, kChunkSize);
    const snt::voxel::VoxelChunk* const chunk = chunks.get_chunk(
        std::string(dimension_id), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return invalid_state("Creature enclosure intersects an unloaded terrain chunk");
    }
    const int32_t local_x = local_coordinate(block_x, kChunkSize);
    const int32_t local_y = local_coordinate(block_y, kChunkSize);
    const int32_t local_z = local_coordinate(block_z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
        return invalid_state("Creature enclosure intersects an unavailable terrain cell");
    }
    return &chunk->terrain.cell_at(local_x, local_y, local_z);
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerCreatureInteractionService>>
GameServerCreatureInteractionService::create(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    const GameContentRegistry& content, GameWildCreatureSystem& wildlife,
    GameServerCreatureInteractionConfig config,
    const IGameServerCreaturePenValidator* pen_validator) {
    if (!std::isfinite(config.unarmed_damage) || config.unarmed_damage <= 0.0f ||
        !std::isfinite(config.feed_tame_progress) || config.feed_tame_progress <= 0.0f ||
        config.feed_tame_progress > 1.0f || config.terrain_pen_search_radius_blocks == 0 ||
        config.terrain_pen_search_radius_blocks > kMaxTerrainPenSearchRadiusBlocks ||
        config.terrain_pen_max_cells == 0 ||
        config.terrain_pen_max_cells > kMaxTerrainPenCells) {
        return invalid_argument("Dedicated server creature interaction configuration is invalid");
    }
    return std::unique_ptr<GameServerCreatureInteractionService>(
        new GameServerCreatureInteractionService(
            player_state, chunks, content, wildlife, std::move(config), pen_validator));
}

GameServerCreatureInteractionService::GameServerCreatureInteractionService(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    const GameContentRegistry& content, GameWildCreatureSystem& wildlife,
    GameServerCreatureInteractionConfig config,
    const IGameServerCreaturePenValidator* pen_validator) noexcept
    : player_state_(&player_state), chunks_(&chunks), content_(&content), wildlife_(&wildlife),
      config_(std::move(config)), pen_validator_(pen_validator) {}

snt::core::Expected<void> GameServerCreatureInteractionService::attack_creature(
    const GameAuthenticatedPeer& peer, const GameCreatureAttackCommand& command,
    uint64_t source_tick) {
    if (auto result = validate_game_creature_attack_command(command); !result) {
        return result.error();
    }
    if (wildlife_ == nullptr) return invalid_state("Creature attack service has no wildlife system");
    const auto creature = wildlife_->find_wild_creature(command.creature_entity_id);
    if (!creature) return invalid_state("Creature attack target is no longer interactable");
    if (auto result = validate_reach(peer, *creature); !result) return result.error();

    const GameWildCreatureAttackResult result = wildlife_->apply_damage(
        command.creature_entity_id, attack_damage_for(peer), source_tick);
    if (!result.hit) return invalid_state("Creature attack target changed before host commit");
    if (result.killed) {
        SNT_LOG_INFO("Authoritative creature kill: player='%s' creature=%llu species=%u",
                     peer.identity.account_id.c_str(),
                     static_cast<unsigned long long>(result.wild_entity_id),
                     static_cast<unsigned>(result.species_id));
    }
    return {};
}

snt::core::Expected<void> GameServerCreatureInteractionService::capture_creature(
    const GameAuthenticatedPeer& peer, const GameCreatureCaptureCommand& command,
    uint64_t source_tick) {
    if (auto result = validate_game_creature_capture_command(command); !result) {
        return result.error();
    }
    if (wildlife_ == nullptr) return invalid_state("Creature capture service has no wildlife system");
    const auto creature = wildlife_->find_wild_creature(command.creature_entity_id);
    if (!creature) return invalid_state("Creature capture target is no longer interactable");
    if (auto result = validate_reach(peer, *creature); !result) return result.error();
    auto pen = validate_capture_pen(*creature);
    if (!pen) return pen.error();

    auto captured = wildlife_->capture_wild_creature(
        {
            .wild_entity_id = command.creature_entity_id,
            .captive_chunk = creature->chunk,
            .pen_bounds = *pen,
        },
        source_tick);
    if (!captured) return captured.error();
    if (!captured->captured) {
        return invalid_state("Creature capture target changed before host commit");
    }
    SNT_LOG_INFO("Authoritative creature capture: player='%s' creature=%llu species=%u",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(captured->captive_entity_id),
                 static_cast<unsigned>(captured->species_id));
    return {};
}

snt::core::Expected<void> GameServerCreatureInteractionService::feed_captive_creature(
    const GameAuthenticatedPeer& peer, const GameCaptiveCreatureFeedCommand& command,
    uint64_t source_tick) {
    if (auto result = validate_game_captive_creature_feed_command(command); !result) {
        return result.error();
    }
    if (wildlife_ == nullptr || player_state_ == nullptr || content_ == nullptr) {
        return invalid_state("Creature feed service is not initialized");
    }
    const auto creature = wildlife_->find_captive_creature(command.creature_entity_id);
    if (!creature || !creature->is_captive) {
        return invalid_state("Captive creature feed target is no longer available");
    }
    if (creature->is_tamed) {
        return invalid_state("Captive creature is already fully tamed");
    }
    if (auto result = validate_reach(peer, *creature); !result) return result.error();
    if (content_->find_item(command.feed_item_id) == nullptr) {
        return invalid_argument("Captive creature feed item is not in the active content catalog");
    }

    const GamePlayerInventoryTransaction transaction{
        .removals = {{.item_id = command.feed_item_id, .count = 1}},
    };
    auto can_consume = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!can_consume) return can_consume.error();
    if (!*can_consume) {
        return invalid_state("Player inventory no longer contains the requested creature feed item");
    }
    auto fed = wildlife_->feed_captive_creature(
        command.creature_entity_id, config_.feed_tame_progress, source_tick);
    if (!fed) return fed.error();
    if (!fed->fed) return invalid_state("Captive creature changed before host feed commit");
    if (auto consumed = player_state_->apply_inventory_transaction(peer, transaction); !consumed) {
        return consumed.error();
    }
    if (fed->became_tamed) {
        SNT_LOG_INFO("Authoritative creature tamed: player='%s' creature=%llu species=%u",
                     peer.identity.account_id.c_str(),
                     static_cast<unsigned long long>(fed->captive_entity_id),
                     static_cast<unsigned>(fed->species_id));
    }
    return {};
}

snt::core::Expected<void> GameServerCreatureInteractionService::validate_reach(
    const GameAuthenticatedPeer& peer,
    const GameCreaturePresentationState& creature) const {
    if (player_state_ == nullptr) return invalid_state("Creature interaction service has no player state");
    auto reachable = player_state_->is_point_reachable(
        peer, creature.chunk.dimension_id, creature.position_x,
        creature.position_y, creature.position_z);
    if (!reachable) return reachable.error();
    if (!*reachable) {
        return invalid_state("Creature interaction target is outside authoritative player reach");
    }
    return {};
}

snt::core::Expected<GameCreaturePenBounds>
GameServerCreatureInteractionService::validate_capture_pen(
    const GameCreaturePresentationState& creature) const {
    if (chunks_ == nullptr) return invalid_state("Creature capture service has no chunk registry");
    if (pen_validator_ != nullptr) return pen_validator_->validate_pen(creature, *chunks_);
    return validate_terrain_pen(creature);
}

snt::core::Expected<GameCreaturePenBounds>
GameServerCreatureInteractionService::validate_terrain_pen(
    const GameCreaturePresentationState& creature) const {
    if (chunks_ == nullptr) return invalid_state("Creature capture service has no chunk registry");
    auto origin_x = floor_to_block(creature.position_x);
    if (!origin_x) return origin_x.error();
    auto origin_y = floor_to_block(creature.position_y);
    if (!origin_y) return origin_y.error();
    auto origin_z = floor_to_block(creature.position_z);
    if (!origin_z) return origin_z.error();

    struct PenCell {
        int32_t x = 0;
        int32_t z = 0;
    };
    const auto walkable = [this, &creature, origin_y = *origin_y](int32_t x, int32_t z)
        -> snt::core::Expected<bool> {
        auto space = terrain_cell_at(*chunks_, creature.chunk.dimension_id, x, origin_y, z);
        if (!space) return space.error();
        auto floor = terrain_cell_at(*chunks_, creature.chunk.dimension_id, x, origin_y - 1, z);
        if (!floor) return floor.error();
        return !(*space)->is_solid() && !(*space)->has_fluid() && (*floor)->is_solid();
    };

    auto origin_walkable = walkable(*origin_x, *origin_z);
    if (!origin_walkable) return origin_walkable.error();
    if (!*origin_walkable) {
        return invalid_state("Creature capture target is not standing in a valid terrain enclosure");
    }

    std::deque<PenCell> pending;
    std::set<std::pair<int32_t, int32_t>> visited;
    pending.push_back({.x = *origin_x, .z = *origin_z});
    visited.emplace(*origin_x, *origin_z);
    GameCreaturePenBounds bounds{
        .min_x = *origin_x,
        .min_y = *origin_y,
        .min_z = *origin_z,
        .max_x = *origin_x,
        .max_y = *origin_y,
        .max_z = *origin_z,
    };
    constexpr std::array<std::pair<int32_t, int32_t>, 4> kNeighbors{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    }};
    while (!pending.empty()) {
        const PenCell cell = pending.front();
        pending.pop_front();
        bounds.min_x = std::min(bounds.min_x, cell.x);
        bounds.max_x = std::max(bounds.max_x, cell.x);
        bounds.min_z = std::min(bounds.min_z, cell.z);
        bounds.max_z = std::max(bounds.max_z, cell.z);
        for (const auto [offset_x, offset_z] : kNeighbors) {
            const int64_t candidate_x64 = static_cast<int64_t>(cell.x) + offset_x;
            const int64_t candidate_z64 = static_cast<int64_t>(cell.z) + offset_z;
            if (candidate_x64 < std::numeric_limits<int32_t>::min() ||
                candidate_x64 > std::numeric_limits<int32_t>::max() ||
                candidate_z64 < std::numeric_limits<int32_t>::min() ||
                candidate_z64 > std::numeric_limits<int32_t>::max()) {
                return invalid_state("Creature terrain enclosure reaches the world coordinate boundary");
            }
            const int32_t candidate_x = static_cast<int32_t>(candidate_x64);
            const int32_t candidate_z = static_cast<int32_t>(candidate_z64);
            if (visited.contains({candidate_x, candidate_z})) continue;
            auto candidate_walkable = walkable(candidate_x, candidate_z);
            if (!candidate_walkable) return candidate_walkable.error();
            if (!*candidate_walkable) continue;
            const int64_t distance_x = std::abs(static_cast<int64_t>(candidate_x) - *origin_x);
            const int64_t distance_z = std::abs(static_cast<int64_t>(candidate_z) - *origin_z);
            if (distance_x >= config_.terrain_pen_search_radius_blocks ||
                distance_z >= config_.terrain_pen_search_radius_blocks) {
                return invalid_state("Creature terrain enclosure is open beyond the configured search radius");
            }
            if (visited.size() >= config_.terrain_pen_max_cells) {
                return invalid_state("Creature terrain enclosure exceeds the configured cell limit");
            }
            visited.emplace(candidate_x, candidate_z);
            pending.push_back({.x = candidate_x, .z = candidate_z});
        }
    }
    if (!bounds.valid()) return invalid_state("Creature terrain enclosure bounds are invalid");
    return bounds;
}

float GameServerCreatureInteractionService::attack_damage_for(
    const GameAuthenticatedPeer& peer) const {
    if (player_state_ == nullptr || content_ == nullptr) return config_.unarmed_damage;
    auto item_id = player_state_->main_hand_item_id_for_peer(peer);
    if (!item_id || item_id->empty()) return config_.unarmed_damage;
    const GameItemDefinition* const item = content_->find_item(*item_id);
    if (item == nullptr || !item->tool.has_value() ||
        !std::isfinite(item->tool->attack_damage) || item->tool->attack_damage <= 0.0f) {
        return config_.unarmed_damage;
    }
    return item->tool->attack_damage;
}

}  // namespace snt::game::replication
