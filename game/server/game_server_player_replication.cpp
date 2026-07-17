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
                                    GameServerPlayerReplicationConfig config,
                                    std::vector<IGameReplicationValueSource*> value_sources) {
    if (config.horizontal_aoi_radius_blocks > kMaxAoiRadiusBlocks ||
        config.vertical_aoi_radius_blocks > kMaxAoiRadiusBlocks) {
        return invalid_argument("Dedicated server player AOI radius exceeds the configured limit");
    }
    if (config.max_visible_players == 0 ||
        config.max_visible_players > kMaxGameSnapshotEntities) {
        return invalid_argument("Dedicated server player AOI visible-player limit is invalid");
    }
    for (const IGameReplicationValueSource* source : value_sources) {
        if (source == nullptr) {
            return invalid_argument("Dedicated server player replication has a null value source");
        }
    }
    return std::unique_ptr<GameServerPlayerReplication>(
        new GameServerPlayerReplication(player_state, std::move(config), std::move(value_sources)));
}

GameServerPlayerReplication::GameServerPlayerReplication(
    GameServerPlayerState& player_state, GameServerPlayerReplicationConfig config,
    std::vector<IGameReplicationValueSource*> value_sources)
    : player_state_(&player_state), config_(std::move(config)),
      value_sources_(std::move(value_sources)) {}

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
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext& context) {
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
    std::vector<GameReplicationValue> accepted_values;

    // The observer's player value is mandatory for authoritative local
    // presentation. Account-owned values follow it, ahead of remote AOI
    // entities, so a full observer list cannot starve the task book.
    const auto append_player = [&snapshot, &accepted_players, &budget](
                                   const VisiblePlayer& player)
        -> snt::core::Expected<bool> {
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
            return false;
        }
        accepted_players.push_back(player);
        return true;
    };

    auto observer_accepted = append_player(visible->front());
    if (!observer_accepted) return observer_accepted.error();
    if (!*observer_accepted) {
        return invalid_argument(
            "Dedicated server player snapshot cannot fit the observing player within the reliable budget");
    }

    auto values = collect_values(peer, interest, budget, context);
    if (!values) return values.error();
    const size_t value_limit = std::min<size_t>(budget.max_value_snapshots_per_tick,
                                                kMaxGameSnapshotValues);
    accepted_values.reserve(std::min(values->size(), value_limit));
    for (const GameReplicationValue& value : *values) {
        if (snapshot.values.size() >= value_limit) break;
        snapshot.values.push_back(value);
        auto candidate = make_game_snapshot(snapshot);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            snapshot.values.pop_back();
            continue;
        }
        accepted_values.push_back(value);
    }

    for (size_t index = 1; index < visible->size(); ++index) {
        auto accepted = append_player((*visible)[index]);
        if (!accepted) return accepted.error();
        if (!*accepted) break;
    }
    auto message = make_game_snapshot(snapshot);
    if (!message) return message.error();

    PeerBaseline baseline{.snapshot_id = snapshot.snapshot_id};
    for (const VisiblePlayer& player : accepted_players) {
        baseline.players.emplace(player.entity_guid.value, player.state);
    }
    for (const GameReplicationValue& value : accepted_values) {
        baseline.values.emplace(static_cast<uint8_t>(value.kind), value);
    }
    peer_baselines_.insert_or_assign(peer.peer, std::move(baseline));
    ++next_snapshot_id_;
    return std::vector<GameReplicationMessage>{std::move(*message)};
}

snt::core::Expected<std::vector<GameReplicationMessage>> GameServerPlayerReplication::build_deltas(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext& context) {
    const auto baseline = peer_baselines_.find(peer.peer);
    if (baseline == peer_baselines_.end()) {
        return invalid_state("Dedicated server player delta has no observer snapshot baseline");
    }
    if (budget.max_reliable_bytes_per_tick == 0) {
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

    auto desired_values = collect_values(peer, interest, budget, context);
    if (!desired_values) return desired_values.error();
    std::map<uint8_t, GameReplicationValue> desired_values_by_kind;
    for (GameReplicationValue& value : *desired_values) {
        desired_values_by_kind.emplace(static_cast<uint8_t>(value.kind), std::move(value));
    }

    std::vector<GameReplicationValue> value_changes;
    value_changes.reserve(baseline->second.values.size() + desired_values_by_kind.size());
    for (const auto& [kind, value] : baseline->second.values) {
        static_cast<void>(value);
        if (!desired_values_by_kind.contains(kind)) {
            value_changes.push_back({
                .kind = static_cast<GameReplicationValueKind>(kind),
                .operation = GameReplicationValueOperation::kRemove,
            });
        }
    }
    for (const auto& [kind, value] : desired_values_by_kind) {
        const auto known = baseline->second.values.find(kind);
        if (known == baseline->second.values.end() ||
            !same_replication_value(known->second, value)) {
            value_changes.push_back(value);
        }
    }
    if (changes.empty() && value_changes.empty()) return std::vector<GameReplicationMessage>{};

    GameDelta delta{
        .base_snapshot_id = baseline->second.snapshot_id,
        .sequence = baseline->second.next_delta_sequence,
    };
    std::vector<EntityChange> accepted_changes;
    std::vector<GameReplicationValue> accepted_value_changes;
    const size_t value_limit = std::min<size_t>(budget.max_value_snapshots_per_tick,
                                                kMaxGameSnapshotValues);
    accepted_value_changes.reserve(std::min(value_changes.size(), value_limit));
    for (const GameReplicationValue& value : value_changes) {
        if (delta.values.size() >= value_limit) break;
        delta.values.push_back(value);
        auto candidate = make_game_delta(delta);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            delta.values.pop_back();
            continue;
        }
        accepted_value_changes.push_back(value);
    }

    const size_t entity_limit = std::min<size_t>(budget.max_entity_snapshots_per_tick,
                                                 kMaxGameSnapshotEntities);
    accepted_changes.reserve(std::min(changes.size(), entity_limit));
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
    if (delta.entities.empty() && delta.values.empty()) {
        if (!value_changes.empty() && value_limit != 0) {
            return invalid_argument(
                "Dedicated server player delta cannot fit one value within the reliable budget");
        }
        if (!changes.empty() && entity_limit != 0) {
            return invalid_argument(
                "Dedicated server player delta cannot fit one entity within the reliable budget");
        }
        return std::vector<GameReplicationMessage>{};
    }
    auto message = make_game_delta(delta);
    if (!message) return message.error();

    for (const GameReplicationValue& value : accepted_value_changes) {
        const uint8_t kind = static_cast<uint8_t>(value.kind);
        if (value.operation == GameReplicationValueOperation::kRemove) {
            baseline->second.values.erase(kind);
        } else {
            baseline->second.values.insert_or_assign(kind, value);
        }
    }
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
                                                        std::string_view reason) noexcept {
    peer_baselines_.erase(peer.peer);
    for (IGameReplicationValueSource* source : value_sources_) {
        source->on_peer_disconnected(peer, reason);
    }
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

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerPlayerReplication::collect_values(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget,
    const snt::network::ReplicationTickContext& context) const {
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0 ||
        value_sources_.empty()) {
        return std::vector<GameReplicationValue>{};
    }

    const size_t value_limit = std::min<size_t>(budget.max_value_snapshots_per_tick,
                                                kMaxGameSnapshotValues);
    std::set<uint8_t> kinds;
    std::vector<GameReplicationValue> values;
    values.reserve(value_limit);
    for (IGameReplicationValueSource* source : value_sources_) {
        auto source_values = source->collect_values(peer, interest, budget, context);
        if (!source_values) {
            auto error = source_values.error();
            error.with_context("GameServerPlayerReplication::collect_values(source)");
            return error;
        }
        for (GameReplicationValue& value : *source_values) {
            if (value.operation != GameReplicationValueOperation::kUpsert) {
                return invalid_state(
                    "Dedicated server value source returned a non-upsert current value");
            }
            if (values.size() >= value_limit) {
                return invalid_argument(
                    "Dedicated server value sources exceeded the configured value budget");
            }
            if (!kinds.insert(static_cast<uint8_t>(value.kind)).second) {
                return invalid_state(
                    "Dedicated server value sources returned duplicate value kinds");
            }
            values.push_back(std::move(value));
        }
    }

    std::sort(values.begin(), values.end(), [](const GameReplicationValue& left,
                                               const GameReplicationValue& right) {
        return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    });
    // Reuse the public snapshot codec as the single authoritative validation
    // of kind, payload size, and duplicate-free value shape.
    auto validation = make_game_snapshot({.snapshot_id = 1, .values = values});
    if (!validation) {
        auto error = validation.error();
        error.with_context("GameServerPlayerReplication::collect_values(validate)");
        return error;
    }
    return values;
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

bool GameServerPlayerReplication::same_replication_value(const GameReplicationValue& left,
                                                          const GameReplicationValue& right) noexcept {
    return left.kind == right.kind && left.operation == right.operation &&
           left.payload == right.payload;
}

}  // namespace snt::game::replication
