// Dedicated-server bed, grave, death, and respawn service implementation.

#define SNT_LOG_CHANNEL "game.server_player_death"
#include "game/server/game_server_player_death.h"

#include "core/error.h"
#include "core/log.h"
#include "game/server/game_server_player_state.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/terrain_data.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr uint64_t kGraveIdFlag = uint64_t{1} << 63;
constexpr uint64_t kGraveSerialMask = ~kGraveIdFlag;
constexpr uint32_t kMaxVerticalGraveSearchBlocks = 512;
constexpr uint32_t kMaxRespawnSearchRadiusBlocks = 128;
constexpr char kPlayerGraveTypeDataPrefix[] = "{\"kind\":\"player_grave\",\"grave_id\":";

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

[[nodiscard]] ChunkKey chunk_key_for_position(const GamePlayerWorldPosition& position) {
    constexpr int kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return {
        position.dimension_id,
        floor_divide(position.position.x, kChunkSize),
        floor_divide(position.position.y, kChunkSize),
        floor_divide(position.position.z, kChunkSize),
    };
}

[[nodiscard]] const snt::voxel::TerrainCell* find_cell(
    const snt::voxel::ChunkRegistry& chunks,
    const GamePlayerWorldPosition& position) {
    constexpr int kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const ChunkKey key = chunk_key_for_position(position);
    const snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) return nullptr;
    const int local_x = local_coordinate(position.position.x, kChunkSize);
    const int local_y = local_coordinate(position.position.y, kChunkSize);
    const int local_z = local_coordinate(position.position.z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return nullptr;
    return &chunk->terrain.cell_at(local_x, local_y, local_z);
}

[[nodiscard]] snt::voxel::TerrainCell* find_mutable_cell(
    snt::voxel::ChunkRegistry& chunks,
    const GamePlayerWorldPosition& position) {
    constexpr int kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const ChunkKey key = chunk_key_for_position(position);
    snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) return nullptr;
    const int local_x = local_coordinate(position.position.x, kChunkSize);
    const int local_y = local_coordinate(position.position.y, kChunkSize);
    const int local_z = local_coordinate(position.position.z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return nullptr;
    return &chunk->terrain.cell_at(local_x, local_y, local_z);
}

[[nodiscard]] bool checked_offset(int32_t value, int32_t offset, int32_t& out) noexcept {
    const int64_t result = static_cast<int64_t>(value) + offset;
    if (result < std::numeric_limits<int32_t>::min() ||
        result > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    out = static_cast<int32_t>(result);
    return true;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerGraveStore>>
GameServerPlayerGraveStore::create(snt::voxel::ChunkRegistry& chunks,
                                   GameChunkSidecarRegistry& sidecars,
                                   GameServerPlayerGraveConfig config) {
    if (config.grave_material_id > std::numeric_limits<snt::voxel::TerrainMaterialId>::max() ||
        config.air_material_id > std::numeric_limits<snt::voxel::TerrainMaterialId>::max() ||
        config.grave_material_id == config.air_material_id ||
        config.vertical_search_blocks == 0 ||
        config.vertical_search_blocks > kMaxVerticalGraveSearchBlocks) {
        return invalid_argument("Dedicated server player grave configuration is invalid");
    }
    auto next_serial = initial_grave_serial(sidecars);
    if (!next_serial) return next_serial.error();
    return std::unique_ptr<GameServerPlayerGraveStore>(
        new GameServerPlayerGraveStore(chunks, sidecars, std::move(config), *next_serial));
}

GameServerPlayerGraveStore::GameServerPlayerGraveStore(
    snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
    GameServerPlayerGraveConfig config, uint64_t next_grave_serial)
    : chunks_(&chunks), sidecars_(&sidecars), config_(std::move(config)),
      next_grave_serial_(next_grave_serial) {}

snt::core::Expected<uint64_t> GameServerPlayerGraveStore::initial_grave_serial(
    const GameChunkSidecarRegistry& sidecars) {
    uint64_t highest_serial = 0;
    std::set<uint64_t> grave_ids;
    bool invalid_record = false;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const GamePlayerGraveRecord& record : sidecar.player_graves) {
            const uint64_t serial = record.grave_id & kGraveSerialMask;
            if ((record.grave_id & kGraveIdFlag) == 0 || serial == 0 ||
                !grave_ids.insert(record.grave_id).second) {
                invalid_record = true;
                return;
            }
            highest_serial = std::max(highest_serial, serial);
        }
    });
    if (invalid_record) {
        return invalid_state("World sidecars contain invalid or duplicate player grave ids");
    }
    if (highest_serial == kGraveSerialMask) {
        return invalid_state("World sidecars exhausted player grave ids");
    }
    return highest_serial + 1;
}

ChunkKey GameServerPlayerGraveStore::chunk_key_for(const GamePlayerWorldPosition& position) {
    return chunk_key_for_position(position);
}

bool GameServerPlayerGraveStore::is_nonempty_stack(const GamePlayerItemStack& stack) noexcept {
    return stack.is_valid_item();
}

snt::core::Expected<GameServerPlayerGraveStore::PlacementCandidate>
GameServerPlayerGraveStore::find_placement(
    const GamePlayerWorldPosition& death_position) const {
    if (chunks_ == nullptr || death_position.dimension_id.empty()) {
        return invalid_state("Player grave placement has no valid world dimension");
    }
    for (uint32_t offset = 0; offset <= config_.vertical_search_blocks; ++offset) {
        int32_t candidate_y = 0;
        if (!checked_offset(death_position.position.y, static_cast<int32_t>(offset), candidate_y) ||
            candidate_y == std::numeric_limits<int32_t>::min()) {
            continue;
        }
        GamePlayerWorldPosition candidate = death_position;
        candidate.position.y = candidate_y;
        const snt::voxel::TerrainCell* cell = find_cell(*chunks_, candidate);
        if (cell == nullptr || cell->is_solid() || cell->has_fluid() ||
            cell->material != static_cast<snt::voxel::TerrainMaterial>(config_.air_material_id)) {
            continue;
        }
        GamePlayerWorldPosition support = candidate;
        --support.position.y;
        const snt::voxel::TerrainCell* support_cell = find_cell(*chunks_, support);
        if (support_cell == nullptr || !support_cell->is_solid()) continue;

        constexpr int kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
        return PlacementCandidate{
            .chunk_key = chunk_key_for(candidate),
            .position = std::move(candidate),
            .local_x = local_coordinate(death_position.position.x, kChunkSize),
            .local_y = local_coordinate(candidate_y, kChunkSize),
            .local_z = local_coordinate(death_position.position.z, kChunkSize),
        };
    }
    return invalid_state("No loaded safe cell is available for the player grave");
}

GameChunkSidecar* GameServerPlayerGraveStore::mutable_sidecar_for(const ChunkKey& key) {
    if (sidecars_ == nullptr || chunks_ == nullptr) return nullptr;
    if (GameChunkSidecar* existing = sidecars_->get(key)) return existing;
    if (chunks_->get_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z) == nullptr) {
        return nullptr;
    }
    sidecars_->set(key, {});
    return sidecars_->get(key);
}

snt::core::Expected<GamePlayerGraveId> GameServerPlayerGraveStore::create_indestructible_grave(
    const GamePlayerGraveCreateRequest& request) {
    if (chunks_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Player grave store has no world sidecar services");
    }
    if (request.owner_account_id.empty() || request.death_position.dimension_id.empty()) {
        return invalid_argument("Player grave request has an invalid owner or death position");
    }

    std::vector<GamePlayerGraveItemStack> stored_items;
    stored_items.reserve(request.contents.slots.size());
    for (const GamePlayerItemStack& stack : request.contents.slots) {
        if (stack.is_empty()) continue;
        if (!is_nonempty_stack(stack)) {
            return invalid_argument("Player grave request contains an invalid inventory stack");
        }
        stored_items.push_back({
            .resource = stack.resource,
            .instance_data = stack.instance_data,
        });
    }
    if (stored_items.empty()) {
        return invalid_argument("Player grave request has no inventory items to store");
    }
    if (stored_items.size() > 128) {
        return invalid_argument("Player grave request exceeds the current sidecar stack limit");
    }

    auto placement = find_placement(request.death_position);
    if (!placement) return placement.error();
    GameChunkSidecar* sidecar = mutable_sidecar_for(placement->chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Player grave placement has no mutable chunk sidecar");
    }
    if (next_grave_serial_ == 0 || next_grave_serial_ > kGraveSerialMask) {
        return invalid_state("Player grave store exhausted grave ids");
    }

    const GamePlayerGraveId id{.value = kGraveIdFlag | next_grave_serial_};
    const auto overlapping = std::find_if(
        sidecar->player_graves.begin(), sidecar->player_graves.end(),
        [&placement](const GamePlayerGraveRecord& record) {
            return record.root_x == placement->position.position.x &&
                   record.root_y == placement->position.position.y &&
                   record.root_z == placement->position.position.z;
        });
    if (overlapping != sidecar->player_graves.end()) {
        return invalid_state("Player grave placement overlaps an existing grave record");
    }

    sidecar->player_graves.push_back({
        .grave_id = id.value,
        .owner_account_id = request.owner_account_id,
        .death_tick = request.death_tick,
        .root_x = placement->position.position.x,
        .root_y = placement->position.position.y,
        .root_z = placement->position.position.z,
        .items = std::move(stored_items),
    });
    sidecar->block_entities.push_back({
        .id = EntityId{id.value},
        .entity_type = BlockEntityType::CUSTOM,
        .root_x = placement->position.position.x,
        .root_y = placement->position.position.y,
        .root_z = placement->position.position.z,
        .type_data_json = std::string(kPlayerGraveTypeDataPrefix) +
                          std::to_string(id.value) + "}",
        .owned_cell_count = 1,
    });

    snt::voxel::TerrainCell* cell = find_mutable_cell(*chunks_, placement->position);
    if (cell == nullptr) {
        sidecar->block_entities.pop_back();
        sidecar->player_graves.pop_back();
        return invalid_state("Player grave placement cell disappeared before commit");
    }
    cell->material = static_cast<snt::voxel::TerrainMaterial>(config_.grave_material_id);
    cell->flags = snt::voxel::TF_SOLID | snt::voxel::TF_INDESTRUCTIBLE;
    cell->clear_fluid();
    ++next_grave_serial_;
    SNT_LOG_INFO("Created indestructible grave %llu for account '%s' at (%d,%d,%d)",
                 static_cast<unsigned long long>(id.value), request.owner_account_id.c_str(),
                 placement->position.position.x, placement->position.position.y,
                 placement->position.position.z);
    return id;
}

snt::core::Expected<GameServerPlayerGraveStore::GraveLocation>
GameServerPlayerGraveStore::locate_grave(GamePlayerGraveId id) const {
    if (!id.valid() || sidecars_ == nullptr) {
        return invalid_argument("Player grave lookup has an invalid grave id or sidecar store");
    }
    std::optional<GraveLocation> location;
    sidecars_->for_each([&](const ChunkKey& key, GameChunkSidecar& sidecar) {
        if (location.has_value()) return;
        for (size_t index = 0; index < sidecar.player_graves.size(); ++index) {
            if (sidecar.player_graves[index].grave_id != id.value) continue;
            location = GraveLocation{.chunk_key = key, .sidecar = &sidecar, .record_index = index};
            return;
        }
    });
    if (!location.has_value()) return invalid_state("Player grave does not exist in loaded world sidecars");
    return std::move(*location);
}

snt::core::Expected<GamePlayerGraveContents> GameServerPlayerGraveStore::read_located_grave(
    const GraveLocation& location, const GamePlayerGraveAccess& access) const {
    if (location.sidecar == nullptr || location.record_index >= location.sidecar->player_graves.size()) {
        return invalid_state("Player grave location is no longer valid");
    }
    const GamePlayerGraveRecord& record = location.sidecar->player_graves[location.record_index];
    if (access.requester_account_id.empty() ||
        (!access.is_administrator && access.requester_account_id != record.owner_account_id)) {
        return invalid_state("Player grave access is not authorized for this account");
    }
    GamePlayerGraveContents contents{
        .id = {.value = record.grave_id},
        .owner_account_id = record.owner_account_id,
        .position = {
            .dimension_id = location.chunk_key.dimension_id,
            .position = {.x = record.root_x, .y = record.root_y, .z = record.root_z},
        },
        .death_tick = record.death_tick,
    };
    contents.items.reserve(record.items.size());
    for (const GamePlayerGraveItemStack& item : record.items) {
        contents.items.push_back({
            .resource = item.resource,
            .instance_data = item.instance_data,
        });
    }
    return contents;
}

snt::core::Expected<GamePlayerGraveContents> GameServerPlayerGraveStore::read_grave(
    GamePlayerGraveId id, const GamePlayerGraveAccess& access) const {
    auto location = locate_grave(id);
    if (!location) return location.error();
    return read_located_grave(*location, access);
}

snt::core::Expected<void> GameServerPlayerGraveStore::erase_grave(
    GamePlayerGraveId id, const GamePlayerGraveAccess& access) {
    if (chunks_ == nullptr) return invalid_state("Player grave store has no voxel chunks");
    auto location = locate_grave(id);
    if (!location) return location.error();
    auto contents = read_located_grave(*location, access);
    if (!contents) return contents.error();

    const GamePlayerWorldPosition grave_position = contents->position;
    snt::voxel::TerrainCell* cell = find_mutable_cell(*chunks_, grave_position);
    if (cell == nullptr) {
        return invalid_state("Player grave cell is not loaded during erase");
    }
    auto& block_entities = location->sidecar->block_entities;
    block_entities.erase(std::remove_if(block_entities.begin(), block_entities.end(),
                                        [id](const BlockEntityPlacement& placement) {
                                            return placement.id.id == id.value;
                                        }),
                         block_entities.end());
    location->sidecar->player_graves.erase(
        location->sidecar->player_graves.begin() +
        static_cast<std::ptrdiff_t>(location->record_index));
    if (cell->material == static_cast<snt::voxel::TerrainMaterial>(config_.grave_material_id) &&
        cell->is_indestructible()) {
        *cell = {
            .material = static_cast<snt::voxel::TerrainMaterial>(config_.air_material_id),
        };
    }
    SNT_LOG_INFO("Removed claimed player grave %llu for account '%s'",
                 static_cast<unsigned long long>(id.value), contents->owner_account_id.c_str());
    return {};
}

size_t GameServerPlayerGraveStore::active_grave_count() const noexcept {
    if (sidecars_ == nullptr) return 0;
    size_t count = 0;
    sidecars_->for_each([&count](const ChunkKey&, const GameChunkSidecar& sidecar) {
        count += sidecar.player_graves.size();
    });
    return count;
}

snt::core::Expected<std::unique_ptr<GameServerPlayerBedService>>
GameServerPlayerBedService::create(GameServerPlayerState& player_state,
                                   const snt::voxel::ChunkRegistry& chunks,
                                   GameChunkSidecarRegistry& sidecars,
                                   IGameServerPlayerStateCheckpointSink* checkpoint_sink) {
    return std::unique_ptr<GameServerPlayerBedService>(
        new GameServerPlayerBedService(player_state, chunks, sidecars, checkpoint_sink));
}

GameServerPlayerBedService::GameServerPlayerBedService(
    GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, IGameServerPlayerStateCheckpointSink* checkpoint_sink)
    : player_state_(&player_state), chunks_(&chunks), sidecars_(&sidecars),
      checkpoint_sink_(checkpoint_sink) {}

ChunkKey GameServerPlayerBedService::chunk_key_for(const GamePlayerWorldPosition& position) {
    return chunk_key_for_position(position);
}

snt::core::Expected<void> GameServerPlayerBedService::on_bed_placed(
    const GamePlayerWorldPosition& position) {
    if (chunks_ == nullptr || sidecars_ == nullptr || position.dimension_id.empty()) {
        return invalid_argument("Player bed placement has invalid world services or position");
    }
    const ChunkKey key = chunk_key_for(position);
    if (chunks_->get_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z) == nullptr) {
        return invalid_state("Player bed placement requires a loaded chunk");
    }
    GameChunkSidecar* sidecar = sidecars_->get(key);
    if (sidecar == nullptr) {
        sidecars_->set(key, {});
        sidecar = sidecars_->get(key);
    }
    if (sidecar == nullptr) return invalid_state("Player bed placement could not create sidecar");
    const auto existing = std::find_if(
        sidecar->player_beds.begin(), sidecar->player_beds.end(),
        [&position](const GamePlayerBedRecord& record) {
            return record.root_x == position.position.x &&
                   record.root_y == position.position.y &&
                   record.root_z == position.position.z;
        });
    if (existing == sidecar->player_beds.end()) {
        sidecar->player_beds.push_back({
            .root_x = position.position.x,
            .root_y = position.position.y,
            .root_z = position.position.z,
        });
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerBedService::on_bed_removed(
    const GamePlayerWorldPosition& position) {
    if (sidecars_ == nullptr || position.dimension_id.empty()) {
        return invalid_argument("Player bed removal has invalid world services or position");
    }
    GameChunkSidecar* sidecar = sidecars_->get(chunk_key_for(position));
    if (sidecar == nullptr) return invalid_state("Player bed removal has no matching chunk sidecar");
    const auto before = sidecar->player_beds.size();
    std::erase_if(sidecar->player_beds, [&position](const GamePlayerBedRecord& record) {
        return record.root_x == position.position.x &&
               record.root_y == position.position.y &&
               record.root_z == position.position.z;
    });
    if (sidecar->player_beds.size() == before) {
        return invalid_state("Player bed removal has no matching bed record");
    }
    return {};
}

snt::core::Expected<bool> GameServerPlayerBedService::has_bed_at(
    const GamePlayerWorldPosition& position) const {
    if (sidecars_ == nullptr || position.dimension_id.empty()) {
        return invalid_argument("Player bed lookup has invalid world services or position");
    }
    const GameChunkSidecar* sidecar = sidecars_->get(chunk_key_for(position));
    if (sidecar == nullptr) return false;
    return std::any_of(sidecar->player_beds.begin(), sidecar->player_beds.end(),
                       [&position](const GamePlayerBedRecord& record) {
                           return record.root_x == position.position.x &&
                                  record.root_y == position.position.y &&
                                  record.root_z == position.position.z;
                       });
}

snt::core::Expected<void> GameServerPlayerBedService::set_respawn_point_from_bed(
    const GameAuthenticatedPeer& peer, const GamePlayerWorldPosition& bed_position) {
    if (player_state_ == nullptr) return invalid_state("Player bed service has no player state");
    auto present = has_bed_at(bed_position);
    if (!present) return present.error();
    if (!*present) return invalid_state("Player respawn point must reference a placed bed");
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) return result.error();
    }
    return player_state_->set_respawn_point(peer, bed_position);
}

snt::core::Expected<void> GameServerPlayerBedService::clear_respawn_point(
    const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) return invalid_state("Player bed service has no player state");
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) return result.error();
    }
    return player_state_->set_respawn_point(peer, std::nullopt);
}

snt::core::Expected<std::unique_ptr<GameServerPlayerRespawnResolver>>
GameServerPlayerRespawnResolver::create(const snt::voxel::ChunkRegistry& chunks,
                                        const IGamePlayerBedLocator& beds,
                                        GameServerPlayerRespawnConfig config) {
    if (config.world_spawn.dimension_id.empty() ||
        config.world_spawn_search_radius_blocks > kMaxRespawnSearchRadiusBlocks) {
        return invalid_argument("Dedicated server player respawn configuration is invalid");
    }
    return std::unique_ptr<GameServerPlayerRespawnResolver>(
        new GameServerPlayerRespawnResolver(chunks, beds, std::move(config)));
}

GameServerPlayerRespawnResolver::GameServerPlayerRespawnResolver(
    const snt::voxel::ChunkRegistry& chunks, const IGamePlayerBedLocator& beds,
    GameServerPlayerRespawnConfig config)
    : chunks_(&chunks), beds_(&beds), config_(std::move(config)) {}

bool GameServerPlayerRespawnResolver::is_safe_feet_position(
    const GamePlayerWorldPosition& position) const {
    if (chunks_ == nullptr || position.position.y == std::numeric_limits<int32_t>::min() ||
        position.position.y >= std::numeric_limits<int32_t>::max() - 1) {
        return false;
    }
    const snt::voxel::TerrainCell* feet = find_cell(*chunks_, position);
    if (feet == nullptr || feet->is_solid() || feet->has_fluid()) return false;
    GamePlayerWorldPosition head = position;
    ++head.position.y;
    const snt::voxel::TerrainCell* head_cell = find_cell(*chunks_, head);
    if (head_cell == nullptr || head_cell->is_solid() || head_cell->has_fluid()) return false;
    GamePlayerWorldPosition support = position;
    --support.position.y;
    const snt::voxel::TerrainCell* support_cell = find_cell(*chunks_, support);
    return support_cell != nullptr && support_cell->is_solid();
}

std::optional<GamePlayerWorldPosition> GameServerPlayerRespawnResolver::find_safe_near(
    const GamePlayerWorldPosition& anchor, uint32_t radius) const {
    for (uint32_t ring = 0; ring <= radius; ++ring) {
        const int32_t signed_ring = static_cast<int32_t>(ring);
        for (int32_t vertical = -2; vertical <= 2; ++vertical) {
            for (int32_t dx = -signed_ring; dx <= signed_ring; ++dx) {
                for (int32_t dz = -signed_ring; dz <= signed_ring; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != signed_ring) continue;
                    GamePlayerWorldPosition candidate = anchor;
                    if (!checked_offset(anchor.position.x, dx, candidate.position.x) ||
                        !checked_offset(anchor.position.y, vertical, candidate.position.y) ||
                        !checked_offset(anchor.position.z, dz, candidate.position.z)) {
                        continue;
                    }
                    if (is_safe_feet_position(candidate)) return candidate;
                }
            }
        }
    }
    return std::nullopt;
}

snt::core::Expected<GamePlayerWorldPosition> GameServerPlayerRespawnResolver::resolve_respawn(
    std::string_view account_id,
    const std::optional<GamePlayerWorldPosition>& saved_respawn_point) {
    if (account_id.empty() || chunks_ == nullptr || beds_ == nullptr) {
        return invalid_argument("Player respawn resolution has invalid account or world services");
    }
    if (saved_respawn_point.has_value()) {
        auto has_bed = beds_->has_bed_at(*saved_respawn_point);
        if (!has_bed) return has_bed.error();
        if (*has_bed) {
            if (auto safe = find_safe_near(*saved_respawn_point, 2); safe.has_value()) {
                return std::move(*safe);
            }
            GamePlayerWorldPosition canonical = *saved_respawn_point;
            if (!checked_offset(canonical.position.x, 1, canonical.position.x) ||
                !checked_offset(canonical.position.y, 1, canonical.position.y)) {
                return invalid_state("Player bed respawn position exceeds world coordinate bounds");
            }
            return canonical;
        }
    }
    if (auto safe = find_safe_near(config_.world_spawn,
                                   config_.world_spawn_search_radius_blocks);
        safe.has_value()) {
        return std::move(*safe);
    }
    return invalid_state("No safe world spawn cell is available for player respawn");
}

snt::core::Expected<std::unique_ptr<GameServerPlayerDeathService>>
GameServerPlayerDeathService::create(GameServerPlayerState& player_state,
                                     IGamePlayerGraveStore& grave_store,
                                     IGamePlayerRespawnResolver& respawn_resolver,
                                     IGameServerPlayerStateCheckpointSink* checkpoint_sink,
                                     IGameServerPlayerMotionReset* motion_reset) {
    return std::unique_ptr<GameServerPlayerDeathService>(
        new GameServerPlayerDeathService(player_state, grave_store, respawn_resolver,
                                         checkpoint_sink, motion_reset));
}

GameServerPlayerDeathService::GameServerPlayerDeathService(
    GameServerPlayerState& player_state, IGamePlayerGraveStore& grave_store,
    IGamePlayerRespawnResolver& respawn_resolver,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerPlayerMotionReset* motion_reset)
    : player_state_(&player_state), grave_store_(&grave_store),
      respawn_resolver_(&respawn_resolver), checkpoint_sink_(checkpoint_sink),
      motion_reset_(motion_reset) {}

std::vector<GamePlayerItemStack> GameServerPlayerDeathService::nonempty_inventory_items(
    const GamePlayerInventory& inventory) {
    std::vector<GamePlayerItemStack> items;
    items.reserve(inventory.slots.size());
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (stack.is_empty()) continue;
        items.push_back(stack);
    }
    return items;
}

snt::core::Expected<void> GameServerPlayerDeathService::restore_inventory_after_failed_death(
    const GameAuthenticatedPeer& peer, const std::vector<GamePlayerItemStack>& items,
    GamePlayerGraveId grave_id) {
    if (player_state_ == nullptr || grave_store_ == nullptr) {
        return invalid_state("Player death rollback has no server services");
    }
    if (!items.empty()) {
        if (auto result = player_state_->apply_inventory_transaction(
                peer, {.additions = items});
            !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerDeathService::restore_inventory_after_failed_death");
            return error;
        }
    }
    if (!grave_id.valid()) return {};
    return grave_store_->erase_grave(
        grave_id, {.requester_account_id = peer.identity.account_id});
}

snt::core::Expected<GamePlayerDeathResult> GameServerPlayerDeathService::resolve_death(
    const GameAuthenticatedPeer& peer, uint64_t death_tick) {
    if (player_state_ == nullptr || grave_store_ == nullptr || respawn_resolver_ == nullptr) {
        return invalid_state("Player death service has no authoritative server services");
    }
    auto persistent = player_state_->capture_persistent_state(peer);
    if (!persistent) return persistent.error();
    auto respawn_position = respawn_resolver_->resolve_respawn(
        peer.identity.account_id, persistent->respawn_point);
    if (!respawn_position) return respawn_position.error();
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) return result.error();
    }

    const std::vector<GamePlayerItemStack> dropped_items =
        nonempty_inventory_items(persistent->inventory);
    std::optional<GamePlayerGraveId> grave_id;
    if (!dropped_items.empty()) {
        auto created = grave_store_->create_indestructible_grave({
            .owner_account_id = peer.identity.account_id,
            .death_position = persistent->position,
            .death_tick = death_tick,
            .contents = persistent->inventory,
        });
        if (!created) return created.error();
        grave_id = *created;
        if (auto result = player_state_->apply_inventory_transaction(
                peer, {.removals = dropped_items});
            !result) {
            auto rollback = grave_store_->erase_grave(
                *grave_id, {.requester_account_id = peer.identity.account_id});
            auto error = result.error();
            error.with_context("GameServerPlayerDeathService::resolve_death(clear inventory)");
            if (!rollback) {
                error.with_context("Player grave rollback also failed: " + rollback.error().format());
            }
            return error;
        }
    }

    if (auto result = player_state_->set_authoritative_position(peer, *respawn_position); !result) {
        auto rollback = restore_inventory_after_failed_death(
            peer, dropped_items, grave_id.value_or(GamePlayerGraveId{}));
        auto error = result.error();
        error.with_context("GameServerPlayerDeathService::resolve_death(set respawn position)");
        if (!rollback) {
            error.with_context("Player death rollback also failed: " + rollback.error().format());
        }
        return error;
    }
    if (motion_reset_ != nullptr) {
        if (auto result = motion_reset_->reset_player_motion(peer); !result) {
            SNT_LOG_ERROR("Unable to reset movement after player death for '%s': %s",
                          peer.identity.account_id.c_str(), result.error().format().c_str());
        }
    }
    SNT_LOG_INFO("Player '%s' died at (%d,%d,%d), respawned at (%d,%d,%d)%s",
                 peer.identity.account_id.c_str(), persistent->position.position.x,
                 persistent->position.position.y, persistent->position.position.z,
                 respawn_position->position.x, respawn_position->position.y,
                 respawn_position->position.z,
                 grave_id.has_value() ? " with an indestructible grave" : " without inventory drops");
    return GamePlayerDeathResult{
        .grave_id = grave_id,
        .respawn_position = std::move(*respawn_position),
    };
}

snt::core::Expected<GamePlayerGraveClaimResult> GameServerPlayerDeathService::reclaim_grave(
    const GameAuthenticatedPeer& peer, GamePlayerGraveId grave_id, bool is_administrator) {
    if (player_state_ == nullptr || grave_store_ == nullptr) {
        return invalid_state("Player grave claim has no authoritative server services");
    }
    const GamePlayerGraveAccess access{
        .requester_account_id = peer.identity.account_id,
        .is_administrator = is_administrator,
    };
    auto contents = grave_store_->read_grave(grave_id, access);
    if (!contents) return contents.error();
    const GamePlayerInventoryTransaction transaction{.additions = contents->items};
    auto applicable = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return GamePlayerGraveClaimResult{
            .status = GamePlayerGraveClaimStatus::kInventoryFull,
            .contents = std::move(*contents),
        };
    }
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) return result.error();
    }
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerDeathService::reclaim_grave(add inventory)");
        return error;
    }
    if (auto result = grave_store_->erase_grave(grave_id, access); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerDeathService::reclaim_grave(erase grave)");
        return error;
    }
    SNT_LOG_INFO("Player '%s' reclaimed grave %llu",
                 peer.identity.account_id.c_str(), static_cast<unsigned long long>(grave_id.value));
    return GamePlayerGraveClaimResult{
        .status = GamePlayerGraveClaimStatus::kCollected,
        .contents = std::move(*contents),
    };
}

}  // namespace snt::game::replication
