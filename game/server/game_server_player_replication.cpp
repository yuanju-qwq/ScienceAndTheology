// Dedicated-server player AOI and snapshot source implementation.

#define SNT_LOG_CHANNEL "game.server_player_replication"
#include "game/server/game_server_player_replication.h"

#include "core/error.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr uint32_t kMaxAoiRadiusBlocks = 32768;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int64_t absolute_delta(int32_t left, int32_t right) noexcept {
    const int64_t delta = static_cast<int64_t>(left) - static_cast<int64_t>(right);
    return delta < 0 ? -delta : delta;
}

[[nodiscard]] bool is_inside_player_aoi(const GameServerPlayerSnapshot& observer,
                                         const GameServerPlayerSnapshot& candidate,
                                         const GameServerPlayerReplicationConfig& config) noexcept {
    if (observer.position.dimension_id != candidate.position.dimension_id) return false;
    const int64_t delta_x = absolute_delta(observer.position.position.x,
                                           candidate.position.position.x);
    const int64_t delta_z = absolute_delta(observer.position.position.z,
                                           candidate.position.position.z);
    if (delta_x > config.horizontal_aoi_radius_blocks ||
        delta_z > config.horizontal_aoi_radius_blocks ||
        absolute_delta(observer.position.position.y, candidate.position.position.y) >
            config.vertical_aoi_radius_blocks) {
        return false;
    }
    const int64_t radius = config.horizontal_aoi_radius_blocks;
    return delta_x * delta_x + delta_z * delta_z <= radius * radius;
}

[[nodiscard]] snt::core::Expected<size_t> encoded_message_size(
    const GameReplicationMessage& message) {
    auto encoded = encode_game_replication_message(message);
    if (!encoded) return encoded.error();
    return encoded->size();
}

struct EntityChange {
    snt::ecs::EntityGuid entity_guid;
    GamePlayerReplicationEntity entity;
};

[[nodiscard]] snt::core::Expected<GameEntitySnapshot> encode_entity_change(
    const EntityChange& change) {
    if (!change.entity_guid.valid()) {
        return invalid_state("Authoritative player replication has an invalid entity guid");
    }
    auto payload = encode_game_player_replication_entity(change.entity);
    if (!payload) return payload.error();
    return GameEntitySnapshot{
        .entity_guid = change.entity_guid,
        .payload = std::move(*payload),
    };
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerReplication>>
GameServerPlayerReplication::create(GameServerPlayerState& player_state,
                                    GameServerPlayerReplicationConfig config) {
    if (config.horizontal_aoi_radius_blocks > kMaxAoiRadiusBlocks ||
        config.vertical_aoi_radius_blocks > kMaxAoiRadiusBlocks) {
        return invalid_argument("Dedicated server player AOI radius exceeds the configured limit");
    }
    if (config.max_visible_players == 0 ||
        config.max_visible_players > kMaxGameSnapshotEntities) {
        return invalid_argument("Dedicated server player AOI visible-player limit is invalid");
    }
    return std::unique_ptr<GameServerPlayerReplication>(
        new GameServerPlayerReplication(player_state, std::move(config)));
}

GameServerPlayerReplication::GameServerPlayerReplication(
    GameServerPlayerState& player_state, GameServerPlayerReplicationConfig config)
    : player_state_(&player_state), config_(std::move(config)) {}

snt::core::Expected<GameReplicationInterest> GameServerPlayerReplication::compute_interest(
    const GameAuthenticatedPeer& peer, const snt::network::ReplicationTickContext&) {
    if (player_state_ == nullptr) {
        return invalid_state("Dedicated server player replication has no player state");
    }
    auto observer = player_state_->snapshot_for_peer(peer);
    if (!observer) return observer.error();
    auto active_players = player_state_->active_player_snapshots();
    if (!active_players) return active_players.error();

    struct Candidate {
        const GameServerPlayerSnapshot* snapshot = nullptr;
        int64_t horizontal_distance_squared = 0;
        int64_t vertical_distance = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(active_players->size());
    for (const GameServerPlayerSnapshot& player : *active_players) {
        if (player.entity_guid == observer->entity_guid) {
            candidates.push_back({.snapshot = &player});
            continue;
        }
        if (!is_inside_player_aoi(*observer, player, config_)) continue;
        const int64_t delta_x = absolute_delta(observer->position.position.x, player.position.position.x);
        const int64_t delta_z = absolute_delta(observer->position.position.z, player.position.position.z);
        candidates.push_back({
            .snapshot = &player,
            .horizontal_distance_squared = delta_x * delta_x + delta_z * delta_z,
            .vertical_distance = absolute_delta(observer->position.position.y, player.position.position.y),
        });
    }
    std::sort(candidates.begin(), candidates.end(), [&observer](const Candidate& left,
                                                                 const Candidate& right) {
        const bool left_is_observer = left.snapshot->entity_guid == observer->entity_guid;
        const bool right_is_observer = right.snapshot->entity_guid == observer->entity_guid;
        if (left_is_observer != right_is_observer) return left_is_observer;
        if (left.horizontal_distance_squared != right.horizontal_distance_squared) {
            return left.horizontal_distance_squared < right.horizontal_distance_squared;
        }
        if (left.vertical_distance != right.vertical_distance) {
            return left.vertical_distance < right.vertical_distance;
        }
        return left.snapshot->entity_guid.value < right.snapshot->entity_guid.value;
    });

    GameReplicationInterest interest;
    const size_t count = std::min<size_t>(candidates.size(), config_.max_visible_players);
    interest.entities.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        interest.entities.push_back(candidates[index].snapshot->entity_guid);
    }
    if (interest.entities.empty() || interest.entities.front() != observer->entity_guid) {
        return invalid_state("Dedicated server player AOI lost the observing player");
    }
    return interest;
}

snt::core::Expected<std::vector<GameReplicationMessage>>
GameServerPlayerReplication::build_initial_snapshot(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&) {
    if (player_state_ == nullptr) {
        return invalid_state("Dedicated server player replication has no player state");
    }
    if (budget.max_entity_snapshots_per_tick == 0 || budget.max_reliable_bytes_per_tick == 0) {
        return std::vector<GameReplicationMessage>{};
    }
    if (next_snapshot_id_ == 0) {
        return invalid_state("Dedicated server player replication snapshot ids are exhausted");
    }

    auto visible = visible_players(interest, budget);
    if (!visible) return visible.error();
    if (visible->empty()) {
        return invalid_state("Dedicated server player AOI has no observing player snapshot");
    }

    GameSnapshot snapshot{.snapshot_id = next_snapshot_id_};
    std::vector<VisiblePlayer> accepted_players;
    accepted_players.reserve(visible->size());
    for (const VisiblePlayer& player : *visible) {
        auto encoded_entity = encode_entity_change({
            .entity_guid = player.entity_guid,
            .entity = {
                .operation = GamePlayerReplicationOperation::kUpsert,
                .player = player.state,
            },
        });
        if (!encoded_entity) return encoded_entity.error();
        snapshot.entities.push_back(std::move(*encoded_entity));
        auto candidate = make_game_snapshot(snapshot);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            snapshot.entities.pop_back();
            break;
        }
        accepted_players.push_back(player);
    }
    if (snapshot.entities.empty()) {
        return invalid_argument(
            "Dedicated server player snapshot cannot fit one player within the reliable budget");
    }
    auto message = make_game_snapshot(snapshot);
    if (!message) return message.error();

    PeerBaseline baseline{.snapshot_id = snapshot.snapshot_id};
    for (const VisiblePlayer& player : accepted_players) {
        baseline.players.emplace(player.entity_guid.value, player.state);
    }
    peer_baselines_.insert_or_assign(peer.peer, std::move(baseline));
    ++next_snapshot_id_;
    return std::vector<GameReplicationMessage>{std::move(*message)};
}

snt::core::Expected<std::vector<GameReplicationMessage>> GameServerPlayerReplication::build_deltas(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&) {
    const auto baseline = peer_baselines_.find(peer.peer);
    if (baseline == peer_baselines_.end()) {
        return invalid_state("Dedicated server player delta has no observer snapshot baseline");
    }
    if (budget.max_entity_snapshots_per_tick == 0 || budget.max_reliable_bytes_per_tick == 0) {
        return std::vector<GameReplicationMessage>{};
    }
    if (baseline->second.next_delta_sequence == 0) {
        return invalid_state("Dedicated server player delta sequences are exhausted");
    }

    auto visible = visible_players(interest, budget);
    if (!visible) return visible.error();
    std::map<uint64_t, VisiblePlayer> desired;
    for (const VisiblePlayer& player : *visible) {
        desired.emplace(player.entity_guid.value, player);
    }

    std::vector<EntityChange> changes;
    changes.reserve(baseline->second.players.size() + desired.size());
    for (const auto& [entity_guid, state] : baseline->second.players) {
        static_cast<void>(state);
        if (!desired.contains(entity_guid)) {
            changes.push_back({
                .entity_guid = {entity_guid},
                .entity = {.operation = GamePlayerReplicationOperation::kRemove},
            });
        }
    }
    for (const VisiblePlayer& player : *visible) {
        const auto known = baseline->second.players.find(player.entity_guid.value);
        if (known == baseline->second.players.end() ||
            !same_player_state(known->second, player.state)) {
            changes.push_back({
                .entity_guid = player.entity_guid,
                .entity = {
                    .operation = GamePlayerReplicationOperation::kUpsert,
                    .player = player.state,
                },
            });
        }
    }
    if (changes.empty()) return std::vector<GameReplicationMessage>{};

    GameDelta delta{
        .base_snapshot_id = baseline->second.snapshot_id,
        .sequence = baseline->second.next_delta_sequence,
    };
    std::vector<EntityChange> accepted_changes;
    const size_t entity_limit = budget.max_entity_snapshots_per_tick;
    for (const EntityChange& change : changes) {
        if (delta.entities.size() >= entity_limit) break;
        auto encoded_entity = encode_entity_change(change);
        if (!encoded_entity) return encoded_entity.error();
        delta.entities.push_back(std::move(*encoded_entity));
        auto candidate = make_game_delta(delta);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            delta.entities.pop_back();
            break;
        }
        accepted_changes.push_back(change);
    }
    if (delta.entities.empty()) {
        return invalid_argument(
            "Dedicated server player delta cannot fit one entity within the reliable budget");
    }
    auto message = make_game_delta(delta);
    if (!message) return message.error();

    for (const EntityChange& change : accepted_changes) {
        if (change.entity.operation == GamePlayerReplicationOperation::kRemove) {
            baseline->second.players.erase(change.entity_guid.value);
        } else {
            baseline->second.players.insert_or_assign(change.entity_guid.value,
                                                      *change.entity.player);
        }
    }
    ++baseline->second.next_delta_sequence;
    return std::vector<GameReplicationMessage>{std::move(*message)};
}

void GameServerPlayerReplication::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                        std::string_view) noexcept {
    peer_baselines_.erase(peer.peer);
}

snt::core::Expected<std::vector<GameServerPlayerReplication::VisiblePlayer>>
GameServerPlayerReplication::visible_players(const GameReplicationInterest& interest,
                                              const GameReplicationBudget& budget) const {
    if (player_state_ == nullptr) {
        return invalid_state("Dedicated server player replication has no player state");
    }
    auto active_players = player_state_->active_player_snapshots();
    if (!active_players) return active_players.error();
    std::map<uint64_t, const GameServerPlayerSnapshot*> snapshots;
    for (const GameServerPlayerSnapshot& snapshot : *active_players) {
        if (!snapshot.entity_guid.valid() ||
            !snapshots.emplace(snapshot.entity_guid.value, &snapshot).second) {
            return invalid_state("Dedicated server player state has duplicate entity snapshots");
        }
    }

    const size_t maximum = std::min<size_t>(config_.max_visible_players,
                                            budget.max_entity_snapshots_per_tick);
    std::set<uint64_t> seen_entities;
    std::vector<VisiblePlayer> visible;
    visible.reserve(std::min(interest.entities.size(), maximum));
    for (const snt::ecs::EntityGuid entity_guid : interest.entities) {
        if (visible.size() >= maximum) break;
        if (!entity_guid.valid() || !seen_entities.insert(entity_guid.value).second) {
            return invalid_argument("Dedicated server player interest contains duplicate entity guids");
        }
        const auto snapshot = snapshots.find(entity_guid.value);
        if (snapshot == snapshots.end()) {
            return invalid_state("Dedicated server player interest references an inactive player");
        }
        auto state = make_player_state(*snapshot->second);
        if (!state) return state.error();
        visible.push_back({.entity_guid = entity_guid, .state = std::move(*state)});
    }
    return visible;
}

snt::core::Expected<GameReplicatedPlayerState> GameServerPlayerReplication::make_player_state(
    const GameServerPlayerSnapshot& snapshot) {
    GameReplicatedPlayerState state{
        .identity = {
            .provider = snapshot.identity_provider,
            .account_id = snapshot.account_id,
            .display_name = snapshot.display_name,
        },
        .position = snapshot.position,
    };
    for (size_t index = 0; index < state.equipment_item_ids.size(); ++index) {
        state.equipment_item_ids[index] = snapshot.equipment.slots[index].item_id;
    }
    if (auto result = validate_game_replicated_player_state(state); !result) return result.error();
    return state;
}

bool GameServerPlayerReplication::same_player_state(const GameReplicatedPlayerState& left,
                                                     const GameReplicatedPlayerState& right) noexcept {
    return left.identity.provider == right.identity.provider &&
           left.identity.account_id == right.identity.account_id &&
           left.identity.display_name == right.identity.display_name &&
           left.position.dimension_id == right.position.dimension_id &&
           left.position.position.x == right.position.position.x &&
           left.position.position.y == right.position.position.y &&
           left.position.position.z == right.position.position.z &&
           left.equipment_item_ids == right.equipment_item_ids;
}

}  // namespace snt::game::replication
