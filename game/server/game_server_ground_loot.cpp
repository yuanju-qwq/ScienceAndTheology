// Dedicated-server ground-loot authority implementation.

#define SNT_LOG_CHANNEL "game.server_ground_loot"
#include "game/server/game_server_ground_loot.h"

#include "core/error.h"
#include "core/log.h"
#include "game/client/game_content_registry.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

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

[[nodiscard]] snt::core::Expected<int32_t> floor_to_block(float value) {
    if (!std::isfinite(value) ||
        value < static_cast<float>(std::numeric_limits<int32_t>::min() + 1) ||
        value > static_cast<float>(std::numeric_limits<int32_t>::max() - 1)) {
        return invalid_argument("Ground loot position cannot be represented as a terrain block");
    }
    return static_cast<int32_t>(std::floor(value));
}

[[nodiscard]] snt::core::Expected<ChunkKey> chunk_key_for_position(
    std::string_view dimension_id, float position_x, float position_y, float position_z) {
    if (dimension_id.empty()) {
        return invalid_argument("Ground loot position has no dimension id");
    }
    auto block_x = floor_to_block(position_x);
    if (!block_x) return block_x.error();
    auto block_y = floor_to_block(position_y);
    if (!block_y) return block_y.error();
    auto block_z = floor_to_block(position_z);
    if (!block_z) return block_z.error();
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return ChunkKey{std::string(dimension_id), floor_divide(*block_x, kChunkSize),
                    floor_divide(*block_y, kChunkSize), floor_divide(*block_z, kChunkSize)};
}

[[nodiscard]] bool is_valid_resource(const ResourceContentStack& resource,
                                     const GameContentRegistry& content) {
    return resource.is_valid() && resource.is_item() &&
           content.find_item(resource.key.id) != nullptr;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerGroundLootService>>
GameServerGroundLootService::create(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerGroundLootStateSink* state_sink, GameServerGroundLootConfig config) {
    if (config.max_loot_per_chunk == 0 ||
        config.max_loot_per_chunk > kMaxGameGroundLootRecordsPerChunk) {
        return invalid_argument("Dedicated server ground loot configuration is invalid");
    }
    auto next_serial = initial_ground_loot_serial(sidecars, content);
    if (!next_serial) return next_serial.error();
    return std::unique_ptr<GameServerGroundLootService>(new GameServerGroundLootService(
        player_state, chunks, sidecars, content, checkpoint_sink, state_sink,
        std::move(config), *next_serial));
}

GameServerGroundLootService::GameServerGroundLootService(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerGroundLootStateSink* state_sink, GameServerGroundLootConfig config,
    uint64_t next_ground_loot_serial) noexcept
    : player_state_(&player_state), chunks_(&chunks), sidecars_(&sidecars), content_(&content),
      checkpoint_sink_(checkpoint_sink), state_sink_(state_sink), config_(std::move(config)),
      next_ground_loot_serial_(next_ground_loot_serial) {}

snt::core::Expected<uint64_t> GameServerGroundLootService::initial_ground_loot_serial(
    const GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content) {
    uint64_t next_serial = 1;
    std::set<uint64_t> seen_ids;
    std::optional<snt::core::Error> failure;
    sidecars.for_each([&](const ChunkKey& chunk, const GameChunkSidecar& sidecar) {
        if (failure.has_value()) return;
        if (sidecar.next_ground_loot_serial == 0 ||
            sidecar.ground_loot.size() > kMaxGameGroundLootRecordsPerChunk) {
            failure = invalid_state("Persisted ground loot sidecar has invalid limits");
            return;
        }
        uint64_t previous_id = 0;
        for (const GameGroundLootRecord& record : sidecar.ground_loot) {
            auto record_chunk = chunk_key_for_position(chunk.dimension_id, record.position_x,
                                                        record.position_y, record.position_z);
            if (!record_chunk || *record_chunk != chunk || record.loot_id == 0 ||
                record.loot_id <= previous_id ||
                record.loot_id == std::numeric_limits<uint64_t>::max() ||
                record.loot_id >= sidecar.next_ground_loot_serial ||
                !is_valid_resource(record.resource, content) ||
                !seen_ids.insert(record.loot_id).second) {
                failure = invalid_state("Persisted ground loot sidecar has an invalid record");
                return;
            }
            previous_id = record.loot_id;
            next_serial = std::max(next_serial, record.loot_id + 1);
        }
        next_serial = std::max(next_serial, sidecar.next_ground_loot_serial);
    });
    if (failure.has_value()) return std::move(*failure);
    return next_serial;
}

snt::core::Expected<void> GameServerGroundLootService::validate_spawn_request(
    const GameGroundLootSpawnRequest& request) const {
    if (chunks_ == nullptr || sidecars_ == nullptr || content_ == nullptr ||
        request.chunk.dimension_id.empty() ||
        !is_valid_resource(request.resource, *content_)) {
        return invalid_argument("Ground loot spawn request is invalid");
    }
    auto position_chunk = chunk_key_for_position(request.chunk.dimension_id, request.position_x,
                                                  request.position_y, request.position_z);
    if (!position_chunk) return position_chunk.error();
    if (*position_chunk != request.chunk) {
        return invalid_argument("Ground loot spawn position is outside its owning chunk");
    }
    if (chunks_->get_chunk(request.chunk.dimension_id, request.chunk.chunk_x,
                            request.chunk.chunk_y, request.chunk.chunk_z) == nullptr) {
        return invalid_state("Ground loot spawn chunk is not loaded");
    }
    return {};
}

snt::core::Expected<void> GameServerGroundLootService::can_spawn_ground_loot(
    std::span<const GameGroundLootSpawnRequest> requests) const {
    if (requests.empty()) return {};
    if (next_ground_loot_serial_ == 0 ||
        next_ground_loot_serial_ >= std::numeric_limits<uint64_t>::max() ||
        requests.size() > std::numeric_limits<uint64_t>::max() - next_ground_loot_serial_) {
        return invalid_state("Ground loot id allocator is exhausted");
    }

    struct ChunkRequestCount {
        ChunkKey chunk;
        size_t count = 0;
    };
    std::vector<ChunkRequestCount> counts;
    counts.reserve(requests.size());
    for (const GameGroundLootSpawnRequest& request : requests) {
        if (auto result = validate_spawn_request(request); !result) return result.error();
        auto found = std::find_if(counts.begin(), counts.end(), [&request](
            const ChunkRequestCount& candidate) { return candidate.chunk == request.chunk; });
        if (found == counts.end()) {
            counts.push_back({.chunk = request.chunk, .count = 1});
        } else {
            ++found->count;
        }
    }
    for (const ChunkRequestCount& count : counts) {
        const GameChunkSidecar* const sidecar = sidecars_->get(count.chunk);
        const size_t existing = sidecar != nullptr ? sidecar->ground_loot.size() : 0;
        if (existing > config_.max_loot_per_chunk ||
            count.count > static_cast<size_t>(config_.max_loot_per_chunk) - existing) {
            return invalid_state("Ground loot chunk capacity is exhausted");
        }
    }
    return {};
}

GameChunkSidecar* GameServerGroundLootService::mutable_sidecar_for(const ChunkKey& chunk) {
    if (GameChunkSidecar* const existing = sidecars_->get(chunk)) return existing;
    sidecars_->set(chunk, {});
    return sidecars_->get(chunk);
}

snt::core::Expected<std::vector<uint64_t>> GameServerGroundLootService::spawn_ground_loot(
    std::span<const GameGroundLootSpawnRequest> requests) {
    if (auto result = can_spawn_ground_loot(requests); !result) return result.error();

    std::vector<uint64_t> ids;
    ids.reserve(requests.size());
    if (requests.empty()) return ids;

    struct ChunkRequestCount {
        ChunkKey chunk;
        size_t count = 0;
    };
    std::vector<ChunkRequestCount> counts;
    counts.reserve(requests.size());
    for (const GameGroundLootSpawnRequest& request : requests) {
        auto found = std::find_if(counts.begin(), counts.end(), [&request](
            const ChunkRequestCount& candidate) { return candidate.chunk == request.chunk; });
        if (found == counts.end()) {
            counts.push_back({.chunk = request.chunk, .count = 1});
        } else {
            ++found->count;
        }
    }
    for (const ChunkRequestCount& count : counts) {
        GameChunkSidecar* const sidecar = mutable_sidecar_for(count.chunk);
        if (sidecar == nullptr) {
            return invalid_state("Ground loot service cannot create a chunk sidecar");
        }
        sidecar->ground_loot.reserve(sidecar->ground_loot.size() + count.count);
    }

    uint64_t latest_spawn_tick = 0;
    for (const GameGroundLootSpawnRequest& request : requests) {
        GameChunkSidecar* const sidecar = sidecars_->get(request.chunk);
        if (sidecar == nullptr) {
            return invalid_state("Ground loot service lost an owning chunk sidecar");
        }
        const uint64_t loot_id = next_ground_loot_serial_;
        ++next_ground_loot_serial_;
        sidecar->ground_loot.push_back({
            .loot_id = loot_id,
            .resource = request.resource,
            .position_x = request.position_x,
            .position_y = request.position_y,
            .position_z = request.position_z,
            .spawned_tick = request.spawned_tick,
        });
        sidecar->next_ground_loot_serial = next_ground_loot_serial_;
        latest_spawn_tick = std::max(latest_spawn_tick, request.spawned_tick);
        ids.push_back(loot_id);
    }
    if (state_sink_ != nullptr) state_sink_->on_ground_loot_state_changed(latest_spawn_tick);
    SNT_LOG_INFO("Spawned %zu authoritative ground loot record(s)", ids.size());
    return ids;
}

snt::core::Expected<GameServerGroundLootService::GroundLootLocation>
GameServerGroundLootService::locate_ground_loot(uint64_t loot_id) const {
    if (loot_id == 0 || sidecars_ == nullptr) {
        return invalid_argument("Ground loot lookup id is invalid");
    }
    std::optional<GroundLootLocation> result;
    sidecars_->for_each([&](const ChunkKey& chunk, GameChunkSidecar& sidecar) {
        if (result.has_value()) return;
        const auto found = std::find_if(
            sidecar.ground_loot.begin(), sidecar.ground_loot.end(),
            [loot_id](const GameGroundLootRecord& record) { return record.loot_id == loot_id; });
        if (found == sidecar.ground_loot.end()) return;
        result = GroundLootLocation{
            .chunk = chunk,
            .sidecar = &sidecar,
            .record_index = static_cast<size_t>(found - sidecar.ground_loot.begin()),
        };
    });
    if (!result) return invalid_state("Ground loot target is no longer available");
    return std::move(*result);
}

snt::core::Expected<void> GameServerGroundLootService::pickup_ground_loot(
    const GameAuthenticatedPeer& peer, const GameGroundLootPickupCommand& command,
    uint64_t source_tick) {
    if (auto result = validate_game_ground_loot_pickup_command(command); !result) {
        return result.error();
    }
    if (player_state_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Ground loot service has no authoritative world services");
    }
    auto location = locate_ground_loot(command.loot_id);
    if (!location) return location.error();
    if (chunks_->get_chunk(location->chunk.dimension_id, location->chunk.chunk_x,
                            location->chunk.chunk_y, location->chunk.chunk_z) == nullptr) {
        return invalid_state("Ground loot target is not in a loaded terrain chunk");
    }
    if (location->sidecar == nullptr ||
        location->record_index >= location->sidecar->ground_loot.size()) {
        return invalid_state("Ground loot target changed before host pickup commit");
    }
    const GameGroundLootRecord record = location->sidecar->ground_loot[location->record_index];
    auto reachable = player_state_->is_point_reachable(
        peer, location->chunk.dimension_id, record.position_x, record.position_y, record.position_z);
    if (!reachable) return reachable.error();
    if (!*reachable) {
        return invalid_state("Ground loot target is outside authoritative player reach");
    }

    const GamePlayerInventoryTransaction transaction{
        .additions = {{.resource = record.resource}},
    };
    auto applicable = player_state_->can_apply_inventory_transaction(peer, transaction);
    if (!applicable) return applicable.error();
    if (!*applicable) {
        return invalid_state("Player inventory cannot accept the requested ground loot");
    }
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) {
            return result.error();
        }
    }
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        auto error = result.error();
        error.with_context("GameServerGroundLootService::pickup_ground_loot(add inventory)");
        return error;
    }
    location->sidecar->ground_loot.erase(
        location->sidecar->ground_loot.begin() + static_cast<std::ptrdiff_t>(location->record_index));
    if (state_sink_ != nullptr) state_sink_->on_ground_loot_state_changed(source_tick);
    SNT_LOG_INFO("Ground loot collected: player='%s' loot=%llu item='%s' amount=%lld",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(record.loot_id),
                 record.resource.key.id.c_str(),
                 static_cast<long long>(record.resource.amount));
    return {};
}

size_t GameServerGroundLootService::active_loot_count() const noexcept {
    if (sidecars_ == nullptr) return 0;
    size_t count = 0;
    sidecars_->for_each([&count](const ChunkKey&, const GameChunkSidecar& sidecar) {
        count += sidecar.ground_loot.size();
    });
    return count;
}

}  // namespace snt::game::replication
