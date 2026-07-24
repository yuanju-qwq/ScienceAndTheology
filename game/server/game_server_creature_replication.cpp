// Dedicated-server creature presentation replication source implementation.

#define SNT_LOG_CHANNEL "game.server_creature_replication"
#include "game/server/game_server_creature_replication.h"

#include "core/error.h"
#include "core/log.h"
#include "game/network/game_creature_replication.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool chunk_is_interesting(
    const ChunkKey& chunk, const GameReplicationInterest& interest) {
    return std::any_of(interest.creature_chunks.begin(), interest.creature_chunks.end(),
                       [&chunk](const snt::voxel::ChunkKey& candidate) {
                           return candidate == chunk;
                       });
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerCreaturePresentationReplication>>
GameServerCreaturePresentationReplication::create(
    GameServerCreaturePresentationReplicationConfig config) {
    if (config.max_visible_creatures == 0 ||
        config.max_visible_creatures > kMaxGameCreaturePresentationStates) {
        return invalid_argument("Dedicated server creature presentation visibility limit is invalid");
    }
    return std::unique_ptr<GameServerCreaturePresentationReplication>(
        new GameServerCreaturePresentationReplication(std::move(config)));
}

GameServerCreaturePresentationReplication::GameServerCreaturePresentationReplication(
    GameServerCreaturePresentationReplicationConfig config) noexcept
    : config_(std::move(config)) {}

void GameServerCreaturePresentationReplication::on_creature_presentation_event(
    const GameCreaturePresentationEvent& event) {
    if (event.creature.entity_id == 0) {
        SNT_LOG_WARN("Ignoring a creature presentation event without a stable entity id");
        return;
    }
    latest_source_tick_ = std::max(latest_source_tick_, event.source_tick);
    switch (event.kind) {
        case GameCreaturePresentationEventKind::kDespawned:
        case GameCreaturePresentationEventKind::kKilled:
            creatures_.erase(event.creature.entity_id);
            return;
        case GameCreaturePresentationEventKind::kSpawned:
        case GameCreaturePresentationEventKind::kUpdated:
        case GameCreaturePresentationEventKind::kDamaged:
        case GameCreaturePresentationEventKind::kCaptured:
        case GameCreaturePresentationEventKind::kTamingProgressed:
        case GameCreaturePresentationEventKind::kTamed:
        case GameCreaturePresentationEventKind::kBreedingStarted:
        case GameCreaturePresentationEventKind::kBorn:
        case GameCreaturePresentationEventKind::kMatured:
            creatures_.insert_or_assign(event.creature.entity_id, event.creature);
            return;
    }
}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerCreaturePresentationReplication::collect_values(
    const GameAuthenticatedPeer&, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase) {
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }

    GameCreaturePresentationSnapshot snapshot;
    // Event time is part of the value only so a client can reject stale
    // reliable deltas. Do not substitute the current replication tick here:
    // that would manufacture a changed payload every server frame.
    snapshot.source_tick = latest_source_tick_;
    snapshot.creatures.reserve(std::min<size_t>(creatures_.size(), config_.max_visible_creatures));
    for (const auto& [entity_id, creature] : creatures_) {
        static_cast<void>(entity_id);
        if (!chunk_is_interesting(creature.chunk, interest)) continue;
        if (snapshot.creatures.size() >= config_.max_visible_creatures) break;
        snapshot.creatures.push_back(creature);
    }
    auto payload = encode_game_creature_presentation_snapshot(snapshot);
    if (!payload) {
        auto error = payload.error();
        error.with_context("GameServerCreaturePresentationReplication::collect_values(encode)");
        return error;
    }
    return std::vector<GameReplicationValue>{
        {
            .kind = GameReplicationValueKind::kCreaturePresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*payload),
        },
    };
}

}  // namespace snt::game::replication
