// Dedicated-server ground-loot authority implementation.

#define SNT_LOG_CHANNEL "game.server_ground_loot"
#include "game/server/game_server_ground_loot.h"

#include "core/error.h"
#include "core/log.h"
#include "game/client/game_content_registry.h"
#include "game/server/game_server_ground_loot_persistence.h"
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

[[nodiscard]] uint64_t saturating_add(uint64_t value, uint64_t amount) noexcept {
    return amount > std::numeric_limits<uint64_t>::max() - value
        ? std::numeric_limits<uint64_t>::max()
        : value + amount;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerGroundLootService>>
GameServerGroundLootService::create(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerGroundLootStateSink* state_sink, GameServerGroundLootConfig config,
    GameServerGroundLootPickupPersistence* pickup_persistence) {
    if (config.max_loot_per_chunk == 0 ||
        config.max_loot_per_chunk > kMaxGameGroundLootRecordsPerChunk ||
        (config.despawn_after_ticks != 0 && config.lifecycle_sweep_interval_ticks == 0)) {
        return invalid_argument("Dedicated server ground loot configuration is invalid");
    }
    auto next_serial = initial_ground_loot_serial(sidecars, content);
    if (!next_serial) return next_serial.error();
    return std::unique_ptr<GameServerGroundLootService>(new GameServerGroundLootService(
        player_state, chunks, sidecars, content, checkpoint_sink, state_sink,
        std::move(config), pickup_persistence, *next_serial));
}

GameServerGroundLootService::GameServerGroundLootService(
    GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink,
    IGameServerGroundLootStateSink* state_sink, GameServerGroundLootConfig config,
    GameServerGroundLootPickupPersistence* pickup_persistence,
    uint64_t next_ground_loot_serial) noexcept
    : player_state_(&player_state), chunks_(&chunks), sidecars_(&sidecars), content_(&content),
      checkpoint_sink_(checkpoint_sink), state_sink_(state_sink),
      pickup_persistence_(pickup_persistence), config_(std::move(config)),
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
        lifetime_last_observed_tick_.emplace(loot_id, request.spawned_tick);
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
    if (pickup_persistence_ != nullptr) {
        const GameGroundLootPickupClaim claim{
            .loot_id = record.loot_id,
            .account_id = peer.identity.account_id,
            .chunk = location->chunk,
            .record = record,
        };
        if (auto result = pickup_persistence_->begin_pickup(claim); !result) {
            auto error = result.error();
            error.with_context("GameServerGroundLootService::pickup_ground_loot(begin journal)");
            return error;
        }
    } else if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) {
            return result.error();
        }
    }
    if (auto result = player_state_->apply_inventory_transaction(peer, transaction); !result) {
        if (pickup_persistence_ != nullptr) {
            if (auto abandoned = pickup_persistence_->abandon_pickup(record.loot_id); !abandoned) {
                SNT_LOG_WARN("Ground loot pickup %llu could not clear its unused journal claim: %s",
                             static_cast<unsigned long long>(record.loot_id),
                             abandoned.error().format().c_str());
            }
        }
        auto error = result.error();
        error.with_context("GameServerGroundLootService::pickup_ground_loot(add inventory)");
        return error;
    }
    bool player_claim_checkpointed = false;
    if (pickup_persistence_ != nullptr) {
        auto checkpointed = pickup_persistence_->checkpoint_player_claim(peer, record.loot_id);
        if (!checkpointed) {
            // The prepared journal remains authoritative if this write was
            // ambiguous or failed. Keep the in-memory transfer singular and
            // let restart recovery choose the durable owner from the receipt.
            SNT_LOG_WARN("Ground loot pickup %llu could not checkpoint its player receipt: %s",
                         static_cast<unsigned long long>(record.loot_id),
                         checkpointed.error().format().c_str());
        } else {
            player_claim_checkpointed = true;
        }
    }
    location->sidecar->ground_loot.erase(
        location->sidecar->ground_loot.begin() + static_cast<std::ptrdiff_t>(location->record_index));
    lifetime_last_observed_tick_.erase(record.loot_id);
    if (pickup_persistence_ != nullptr && player_claim_checkpointed) {
        if (auto result = pickup_persistence_->finalize_pickup(record.loot_id); !result) {
            // Both live owners are already singular. Retaining the journal is
            // intentional here: a controlled shutdown or restart will retry
            // the sidecar checkpoint rather than risking a duplicate stack.
            SNT_LOG_WARN("Ground loot pickup %llu is awaiting sidecar checkpoint: %s",
                         static_cast<unsigned long long>(record.loot_id),
                         result.error().format().c_str());
        }
    }
    if (state_sink_ != nullptr) state_sink_->on_ground_loot_state_changed(source_tick);
    SNT_LOG_INFO("Ground loot collected: player='%s' loot=%llu item='%s' amount=%lld",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(record.loot_id),
                 record.resource.key.id.c_str(),
                 static_cast<long long>(record.resource.amount));
    return {};
}

snt::core::Expected<size_t> GameServerGroundLootService::tick(uint64_t source_tick) {
    if (sidecars_ == nullptr || chunks_ == nullptr) {
        return invalid_state("Ground loot lifecycle has no authoritative world services");
    }
    if (config_.despawn_after_ticks == 0) return size_t{0};
    if (has_last_lifecycle_sweep_tick_ && source_tick < last_lifecycle_sweep_tick_) {
        return invalid_argument("Ground loot lifecycle tick regressed");
    }
    if (has_last_lifecycle_sweep_tick_ &&
        source_tick - last_lifecycle_sweep_tick_ < config_.lifecycle_sweep_interval_ticks) {
        return size_t{0};
    }
    last_lifecycle_sweep_tick_ = source_tick;
    has_last_lifecycle_sweep_tick_ = true;

    size_t removed = 0;
    std::set<uint64_t> resident_loot_ids;
    sidecars_->for_each([this, source_tick, &removed, &resident_loot_ids](
                           const ChunkKey& chunk, GameChunkSidecar& sidecar) {
        if (chunks_->get_chunk(chunk.dimension_id, chunk.chunk_x,
                               chunk.chunk_y, chunk.chunk_z) == nullptr) {
            return;
        }
        for (auto record = sidecar.ground_loot.begin();
             record != sidecar.ground_loot.end();) {
            const auto [last_observed, inserted] = lifetime_last_observed_tick_.try_emplace(
                record->loot_id, source_tick);
            if (!inserted) {
                if (source_tick < last_observed->second) {
                    // Spawn requests and fixed ticks share one source. If a
                    // caller violates that order, rebaseline rather than
                    // allowing unsigned subtraction to expire every record.
                    last_observed->second = source_tick;
                } else {
                    record->lifetime_ticks = saturating_add(
                        record->lifetime_ticks, source_tick - last_observed->second);
                    last_observed->second = source_tick;
                }
            }
            if (record->lifetime_ticks >= config_.despawn_after_ticks) {
                lifetime_last_observed_tick_.erase(record->loot_id);
                record = sidecar.ground_loot.erase(record);
                ++removed;
                continue;
            }
            resident_loot_ids.insert(record->loot_id);
            ++record;
        }
    });
    for (auto observed = lifetime_last_observed_tick_.begin();
         observed != lifetime_last_observed_tick_.end();) {
        if (!resident_loot_ids.contains(observed->first)) {
            observed = lifetime_last_observed_tick_.erase(observed);
        } else {
            ++observed;
        }
    }
    if (removed != 0) {
        if (state_sink_ != nullptr) state_sink_->on_ground_loot_state_changed(source_tick);
        SNT_LOG_INFO("Expired %zu terrain-resident ground loot record(s)", removed);
    }
    return removed;
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
