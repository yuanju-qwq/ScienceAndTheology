// Dedicated-server ground-loot AOI source implementation.

#include "game/server/game_server_ground_loot_replication.h"

#include "core/error.h"
#include "game/network/game_ground_loot_replication.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerGroundLootReplication>>
GameServerGroundLootReplication::create(
    const GameChunkSidecarRegistry& sidecars,
    GameServerGroundLootReplicationConfig config) {
    if (config.max_visible_loot == 0 ||
        config.max_visible_loot > kMaxGameGroundLootPresentationStates) {
        return invalid_argument("Dedicated server ground loot visibility limit is invalid");
    }
    return std::unique_ptr<GameServerGroundLootReplication>(
        new GameServerGroundLootReplication(sidecars, std::move(config)));
}

GameServerGroundLootReplication::GameServerGroundLootReplication(
    const GameChunkSidecarRegistry& sidecars,
    GameServerGroundLootReplicationConfig config) noexcept
    : sidecars_(&sidecars), config_(std::move(config)) {}

void GameServerGroundLootReplication::on_ground_loot_state_changed(
    uint64_t source_tick) noexcept {
    latest_source_tick_ = std::max(latest_source_tick_, source_tick);
}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerGroundLootReplication::collect_values(
    const GameAuthenticatedPeer&, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase) {
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }
    if (sidecars_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Ground loot replication source has no sidecar registry"};
    }

    GameGroundLootPresentationSnapshot snapshot;
    snapshot.source_tick = latest_source_tick_;
    snapshot.loot.reserve(config_.max_visible_loot);
    std::set<uint64_t> visible_loot_ids;
    for (const snt::voxel::ChunkKey& chunk : interest.chunks) {
        const GameChunkSidecar* const sidecar = sidecars_->get(chunk);
        if (sidecar == nullptr) continue;
        for (const GameGroundLootRecord& record : sidecar->ground_loot) {
            if (snapshot.loot.size() >= config_.max_visible_loot) break;
            if (!visible_loot_ids.insert(record.loot_id).second) continue;
            snapshot.loot.push_back({
                .loot_id = record.loot_id,
                .chunk = chunk,
                .resource = record.resource,
                .position_x = record.position_x,
                .position_y = record.position_y,
                .position_z = record.position_z,
                .spawned_tick = record.spawned_tick,
            });
        }
        if (snapshot.loot.size() >= config_.max_visible_loot) break;
    }
    std::sort(snapshot.loot.begin(), snapshot.loot.end(),
              [](const GameGroundLootPresentationState& left,
                 const GameGroundLootPresentationState& right) {
                  return left.loot_id < right.loot_id;
              });
    auto payload = encode_game_ground_loot_presentation_snapshot(snapshot);
    if (!payload) {
        auto error = payload.error();
        error.with_context("GameServerGroundLootReplication::collect_values(encode)");
        return error;
    }
    return std::vector<GameReplicationValue>{
        {
            .kind = GameReplicationValueKind::kGroundLootPresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*payload),
        },
    };
}

}  // namespace snt::game::replication
