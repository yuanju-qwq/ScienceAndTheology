// Dedicated-server player, terrain, and machine replication implementation.

#define SNT_LOG_CHANNEL "game.server_player_replication"
#include "game/server/game_server_player_replication.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/machine_tick_system.h"
#include "game/world/game_chunk.h"
#include "voxel/data/chunk_registry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <tuple>
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

[[nodiscard]] int64_t distance_to_range(int64_t value, int64_t minimum,
                                         int64_t maximum) noexcept {
    if (value < minimum) return minimum - value;
    if (value > maximum) return value - maximum;
    return 0;
}

[[nodiscard]] int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

[[nodiscard]] int32_t local_coordinate(int32_t value, int32_t divisor) noexcept {
    return value - floor_divide(value, divisor) * divisor;
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

struct MachineEntityChange {
    snt::ecs::EntityGuid entity_guid;
    GameMachineReplicationEntity entity;
};

[[nodiscard]] snt::core::Expected<GameEntitySnapshot> encode_machine_entity_change(
    const MachineEntityChange& change) {
    if (!change.entity_guid.valid()) {
        return invalid_state("Authoritative machine replication has an invalid entity guid");
    }
    auto payload = encode_game_machine_replication_entity(change.entity);
    if (!payload) return payload.error();
    return GameEntitySnapshot{
        .entity_guid = change.entity_guid,
        .payload = std::move(*payload),
    };
}

[[nodiscard]] const BlockEntityPlacement* find_machine_anchor(
    const GameChunkSidecar& sidecar, EntityId anchor_entity_id) {
    const auto found = std::find_if(
        sidecar.block_entities.begin(), sidecar.block_entities.end(),
        [anchor_entity_id](const BlockEntityPlacement& placement) {
            return placement.id == anchor_entity_id &&
                   placement.entity_type == BlockEntityType::MACHINE;
        });
    return found == sidecar.block_entities.end() ? nullptr : &*found;
}

[[nodiscard]] bool same_machine_stack(const GameReplicatedMachineItemStack& left,
                                       const GameReplicatedMachineItemStack& right) noexcept {
    return left.item_id == right.item_id && left.count == right.count;
}

[[nodiscard]] bool same_machine_stacks(
    const std::vector<GameReplicatedMachineItemStack>& left,
    const std::vector<GameReplicatedMachineItemStack>& right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), same_machine_stack);
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerReplication>>
GameServerPlayerReplication::create(GameServerPlayerState& player_state, snt::ecs::World& world,
                                    snt::voxel::ChunkRegistry& chunks,
                                    GameChunkSidecarRegistry& sidecars,
                                    GameServerPlayerReplicationConfig config,
                                    std::vector<IGameReplicationValueSource*> value_sources) {
    if (config.horizontal_aoi_radius_blocks > kMaxAoiRadiusBlocks ||
        config.vertical_aoi_radius_blocks > kMaxAoiRadiusBlocks ||
        config.chunk_horizontal_aoi_radius_blocks > kMaxAoiRadiusBlocks ||
        config.chunk_vertical_aoi_radius_blocks > kMaxAoiRadiusBlocks) {
        return invalid_argument("Dedicated server player AOI radius exceeds the configured limit");
    }
    if (config.max_visible_players == 0 ||
        config.max_visible_players > kMaxGameSnapshotEntities) {
        return invalid_argument("Dedicated server player AOI visible-player limit is invalid");
    }
    if (config.max_visible_chunks == 0 ||
        config.max_visible_chunks > kMaxGameSnapshotChunks) {
        return invalid_argument("Dedicated server player replication visible-chunk limit is invalid");
    }
    for (const IGameReplicationValueSource* source : value_sources) {
        if (source == nullptr) {
            return invalid_argument("Dedicated server player replication has a null value source");
        }
    }
    return std::unique_ptr<GameServerPlayerReplication>(
        new GameServerPlayerReplication(player_state, world, chunks, sidecars,
                                        std::move(config), std::move(value_sources)));
}

GameServerPlayerReplication::GameServerPlayerReplication(
    GameServerPlayerState& player_state, snt::ecs::World& world,
    snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
    GameServerPlayerReplicationConfig config,
    std::vector<IGameReplicationValueSource*> value_sources)
    : player_state_(&player_state), world_(&world), chunks_(&chunks), sidecars_(&sidecars),
      config_(std::move(config)),
      value_sources_(std::move(value_sources)) {}

snt::core::Expected<GameReplicationInterest> GameServerPlayerReplication::compute_interest(
    const GameAuthenticatedPeer& peer, const snt::network::ReplicationTickContext&) {
    if (player_state_ == nullptr || world_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr) {
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

    struct ChunkCandidate {
        snt::voxel::ChunkKey key;
        int64_t horizontal_distance_squared = 0;
        int64_t vertical_distance = 0;
    };
    std::vector<ChunkCandidate> chunk_candidates;
    for (const snt::voxel::ChunkKey& key : chunks_->all_chunk_keys()) {
        if (key.dimension_id != observer->position.dimension_id) continue;
        constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
        const int64_t minimum_x = static_cast<int64_t>(key.chunk_x) * kChunkSize;
        const int64_t minimum_y = static_cast<int64_t>(key.chunk_y) * kChunkSize;
        const int64_t minimum_z = static_cast<int64_t>(key.chunk_z) * kChunkSize;
        const int64_t delta_x = distance_to_range(observer->position.position.x, minimum_x,
                                                  minimum_x + kChunkSize - 1);
        const int64_t delta_y = distance_to_range(observer->position.position.y, minimum_y,
                                                  minimum_y + kChunkSize - 1);
        const int64_t delta_z = distance_to_range(observer->position.position.z, minimum_z,
                                                  minimum_z + kChunkSize - 1);
        if (delta_x > config_.chunk_horizontal_aoi_radius_blocks ||
            delta_z > config_.chunk_horizontal_aoi_radius_blocks ||
            delta_y > config_.chunk_vertical_aoi_radius_blocks) {
            continue;
        }
        chunk_candidates.push_back({
            .key = key,
            .horizontal_distance_squared = delta_x * delta_x + delta_z * delta_z,
            .vertical_distance = delta_y,
        });
    }
    std::sort(chunk_candidates.begin(), chunk_candidates.end(), [](const ChunkCandidate& left,
                                                                     const ChunkCandidate& right) {
        if (left.horizontal_distance_squared != right.horizontal_distance_squared) {
            return left.horizontal_distance_squared < right.horizontal_distance_squared;
        }
        if (left.vertical_distance != right.vertical_distance) {
            return left.vertical_distance < right.vertical_distance;
        }
        return GameChunkKeyLess{}(left.key, right.key);
    });
    const size_t chunk_count = std::min<size_t>(chunk_candidates.size(), config_.max_visible_chunks);
    interest.chunks.reserve(chunk_count);
    for (size_t index = 0; index < chunk_count; ++index) {
        interest.chunks.push_back(std::move(chunk_candidates[index].key));
    }

    for (const snt::voxel::ChunkKey& key : interest.chunks) {
        const GameChunkSidecar* sidecar = sidecars_->get(key);
        if (sidecar == nullptr) continue;
        for (const MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
            const snt::ecs::EntityGuid entity_guid{record.entity_guid};
            if (!entity_guid.valid()) {
                return invalid_state("Dedicated server machine replication has an invalid anchored guid");
            }
            const entt::entity entity = world_->find_entity_by_guid(entity_guid);
            if (entity == entt::null ||
                !world_->registry().all_of<MachineRuntimeComponent>(entity) ||
                find_machine_anchor(*sidecar, record.anchor_entity_id) == nullptr) {
                return invalid_state("Dedicated server machine replication found an invalid sidecar anchor");
            }
            interest.detailed_machine_entities.push_back(entity_guid);
        }
    }
    std::sort(interest.detailed_machine_entities.begin(), interest.detailed_machine_entities.end(),
              [](snt::ecs::EntityGuid left, snt::ecs::EntityGuid right) {
                  return left.value < right.value;
              });
    if (std::adjacent_find(interest.detailed_machine_entities.begin(),
                           interest.detailed_machine_entities.end()) !=
        interest.detailed_machine_entities.end()) {
        return invalid_state("Dedicated server machine replication has duplicate anchored guids");
    }
    return interest;
}

snt::core::Expected<std::vector<GameReplicationMessage>>
GameServerPlayerReplication::build_initial_snapshot(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext& context) {
    if (player_state_ == nullptr || chunks_ == nullptr || world_ == nullptr || sidecars_ == nullptr) {
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
    auto visible_chunk_values = visible_chunks(interest);
    if (!visible_chunk_values) return visible_chunk_values.error();
    auto visible_machine_values = visible_machines(interest);
    if (!visible_machine_values) return visible_machine_values.error();

    GameSnapshot snapshot{.snapshot_id = next_snapshot_id_};
    std::vector<VisiblePlayer> accepted_players;
    accepted_players.reserve(visible->size());
    std::vector<VisibleChunk> accepted_chunks;
    accepted_chunks.reserve(visible_chunk_values->size());
    std::vector<VisibleMachine> accepted_machines;
    accepted_machines.reserve(visible_machine_values->size());
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

    const auto append_chunk = [&snapshot, &accepted_chunks, &budget](
                                  const VisibleChunk& chunk)
        -> snt::core::Expected<bool> {
        snapshot.chunks.push_back({.chunk = chunk.key, .payload = chunk.payload});
        auto candidate = make_game_snapshot(snapshot);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            snapshot.chunks.pop_back();
            return false;
        }
        accepted_chunks.push_back(chunk);
        return true;
    };

    const auto append_machine = [&snapshot, &accepted_machines, &budget](
                                    const VisibleMachine& machine)
        -> snt::core::Expected<bool> {
        auto encoded_entity = encode_machine_entity_change({
            .entity_guid = machine.entity_guid,
            .entity = {
                .operation = GameMachineReplicationOperation::kUpsert,
                .machine = machine.state,
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
        accepted_machines.push_back(machine);
        return true;
    };

    auto observer_accepted = append_player(visible->front());
    if (!observer_accepted) return observer_accepted.error();
    if (!*observer_accepted) {
        return invalid_argument(
            "Dedicated server player snapshot cannot fit the observing player within the reliable budget");
    }

    const size_t chunk_limit = std::min<size_t>(budget.max_chunk_snapshots_per_tick,
                                                kMaxGameSnapshotChunks);
    for (const VisibleChunk& chunk : *visible_chunk_values) {
        if (snapshot.chunks.size() >= chunk_limit) break;
        auto accepted = append_chunk(chunk);
        if (!accepted) return accepted.error();
        if (!*accepted) {
            return invalid_argument(
                "Dedicated server terrain snapshot cannot fit one chunk within the reliable budget");
        }
    }

    auto values = collect_values(peer, interest, budget, context,
                                 GameReplicationValueCollectionPhase::kInitialSnapshot);
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

    const size_t entity_limit = std::min<size_t>(budget.max_entity_snapshots_per_tick,
                                                 kMaxGameSnapshotEntities);
    std::set<snt::voxel::ChunkKey, GameChunkKeyLess> accepted_chunk_keys;
    for (const VisibleChunk& chunk : accepted_chunks) accepted_chunk_keys.insert(chunk.key);
    for (const VisibleMachine& machine : *visible_machine_values) {
        if (snapshot.entities.size() >= entity_limit ||
            !accepted_chunk_keys.contains(machine.state.anchor_chunk)) {
            continue;
        }
        auto accepted = append_machine(machine);
        if (!accepted) return accepted.error();
        if (!*accepted) break;
    }

    for (size_t index = 1; index < visible->size(); ++index) {
        if (snapshot.entities.size() >= entity_limit) break;
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
    for (const VisibleChunk& chunk : accepted_chunks) {
        baseline.chunks.emplace(chunk.key, chunk.terrain);
    }
    for (const VisibleMachine& machine : accepted_machines) {
        baseline.machines.emplace(machine.entity_guid.value, machine.state);
    }
    for (const GameReplicationValue& value : accepted_values) {
        baseline.values.emplace(static_cast<uint8_t>(value.kind), value);
    }
    peer_baselines_.insert_or_assign(peer.peer, std::move(baseline));
    notify_values_committed(peer, GameReplicationValueCollectionPhase::kInitialSnapshot,
                            accepted_values);
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
    auto visible_chunk_values = visible_chunks(interest);
    if (!visible_chunk_values) return visible_chunk_values.error();
    auto visible_machine_values = visible_machines(interest);
    if (!visible_machine_values) return visible_machine_values.error();
    std::map<uint64_t, VisiblePlayer> desired;
    for (const VisiblePlayer& player : *visible) {
        desired.emplace(player.entity_guid.value, player);
    }
    std::map<snt::voxel::ChunkKey, VisibleChunk, GameChunkKeyLess> desired_chunks;
    for (const VisibleChunk& chunk : *visible_chunk_values) {
        if (!desired_chunks.emplace(chunk.key, chunk).second) {
            return invalid_argument("Dedicated server chunk interest contains duplicate chunks");
        }
    }
    std::map<uint64_t, VisibleMachine> desired_machines;
    for (const VisibleMachine& machine : *visible_machine_values) {
        if (!desired_machines.emplace(machine.entity_guid.value, machine).second) {
            return invalid_state("Dedicated server machine interest contains duplicate entity guids");
        }
    }

    std::vector<snt::voxel::ChunkKey> removed_chunks;
    removed_chunks.reserve(baseline->second.chunks.size());
    for (const auto& [key, terrain] : baseline->second.chunks) {
        static_cast<void>(terrain);
        if (!desired_chunks.contains(key)) removed_chunks.push_back(key);
    }
    std::vector<VisibleChunk> chunk_snapshots;
    chunk_snapshots.reserve(desired_chunks.size());
    for (const auto& [key, chunk] : desired_chunks) {
        static_cast<void>(key);
        if (!baseline->second.chunks.contains(chunk.key)) chunk_snapshots.push_back(chunk);
    }
    std::vector<GameChunkDelta> block_changes;
    for (const auto& [key, dirty_cells] : dirty_blocks_) {
        if (!desired_chunks.contains(key)) continue;
        const auto known_chunk = baseline->second.chunks.find(key);
        if (known_chunk == baseline->second.chunks.end()) continue;
        GameChunkDelta change{.chunk = key};
        for (const auto& [local_index, block] : dirty_cells) {
            if (local_index >= known_chunk->second.cells.size()) {
                return invalid_state("Dedicated server terrain baseline has an invalid local cell index");
            }
            const GameReplicatedTerrainCell known = known_chunk->second.cells[local_index];
            if (known.material != block.material || known.flags != block.flags) {
                change.blocks.push_back(block);
            }
        }
        if (!change.blocks.empty()) block_changes.push_back(std::move(change));
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

    std::vector<MachineEntityChange> machine_changes;
    machine_changes.reserve(baseline->second.machines.size() + desired_machines.size());
    for (const auto& [entity_guid, state] : baseline->second.machines) {
        static_cast<void>(state);
        if (!desired_machines.contains(entity_guid)) {
            machine_changes.push_back({
                .entity_guid = {entity_guid},
                .entity = {.operation = GameMachineReplicationOperation::kRemove},
            });
        }
    }
    for (const auto& [entity_guid, machine] : desired_machines) {
        const auto known = baseline->second.machines.find(entity_guid);
        if (known == baseline->second.machines.end() ||
            !same_machine_state(known->second, machine.state)) {
            machine_changes.push_back({
                .entity_guid = machine.entity_guid,
                .entity = {
                    .operation = GameMachineReplicationOperation::kUpsert,
                    .machine = machine.state,
                },
            });
        }
    }

    auto desired_values = collect_values(peer, interest, budget, context,
                                         GameReplicationValueCollectionPhase::kDelta);
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
    if (changes.empty() && machine_changes.empty() && value_changes.empty() &&
        chunk_snapshots.empty() && removed_chunks.empty() && block_changes.empty()) {
        return std::vector<GameReplicationMessage>{};
    }

    GameDelta delta{
        .base_snapshot_id = baseline->second.snapshot_id,
        .sequence = baseline->second.next_delta_sequence,
    };
    std::vector<VisibleChunk> accepted_chunk_snapshots;
    std::vector<snt::voxel::ChunkKey> accepted_removed_chunks;
    std::vector<GameChunkDelta> accepted_block_changes;

    const size_t chunk_snapshot_limit = std::min<size_t>(
        budget.max_chunk_snapshots_per_tick, kMaxGameDeltaChunks);
    for (const VisibleChunk& chunk : chunk_snapshots) {
        if (delta.chunk_snapshots.size() >= chunk_snapshot_limit) break;
        delta.chunk_snapshots.push_back({.chunk = chunk.key, .payload = chunk.payload});
        auto candidate = make_game_delta(delta);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            delta.chunk_snapshots.pop_back();
            if (accepted_chunk_snapshots.empty()) {
                return invalid_argument(
                    "Dedicated server terrain delta cannot fit one chunk within the reliable budget");
            }
            break;
        }
        accepted_chunk_snapshots.push_back(chunk);
    }

    for (const snt::voxel::ChunkKey& key : removed_chunks) {
        if (delta.removed_chunks.size() >= kMaxGameDeltaChunks) break;
        delta.removed_chunks.push_back(key);
        auto candidate = make_game_delta(delta);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            delta.removed_chunks.pop_back();
            break;
        }
        accepted_removed_chunks.push_back(key);
    }

    const size_t block_limit = std::min<size_t>(budget.max_block_deltas_per_tick,
                                                kMaxGameBlockDeltas);
    size_t accepted_block_count = 0;
    for (const GameChunkDelta& change : block_changes) {
        if (accepted_block_count >= block_limit ||
            delta.chunks.size() >= kMaxGameDeltaChunks) {
            break;
        }
        GameChunkDelta accepted{.chunk = change.chunk};
        for (const GameBlockDelta& block : change.blocks) {
            if (accepted_block_count + accepted.blocks.size() >= block_limit ||
                accepted.blocks.size() >= kMaxGameBlockDeltasPerChunk) {
                break;
            }
            accepted.blocks.push_back(block);
        }
        if (accepted.blocks.empty()) continue;
        delta.chunks.push_back(accepted);
        auto candidate = make_game_delta(delta);
        if (!candidate) return candidate.error();
        auto size = encoded_message_size(*candidate);
        if (!size) return size.error();
        if (*size > budget.max_reliable_bytes_per_tick) {
            delta.chunks.pop_back();
            break;
        }
        accepted_block_count += accepted.blocks.size();
        accepted_block_changes.push_back(std::move(accepted));
    }

    std::vector<EntityChange> accepted_changes;
    std::vector<MachineEntityChange> accepted_machine_changes;
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
    for (const MachineEntityChange& change : machine_changes) {
        if (delta.entities.size() >= entity_limit) break;
        if (change.entity.operation == GameMachineReplicationOperation::kUpsert) {
            const snt::voxel::ChunkKey& anchor_chunk = change.entity.machine->anchor_chunk;
            const bool has_chunk_baseline = baseline->second.chunks.contains(anchor_chunk) ||
                std::any_of(accepted_chunk_snapshots.begin(), accepted_chunk_snapshots.end(),
                            [&anchor_chunk](const VisibleChunk& chunk) {
                                return chunk.key == anchor_chunk;
                            });
            if (!has_chunk_baseline) continue;
        }
        const bool collides_with_player = std::any_of(
            accepted_changes.begin(), accepted_changes.end(),
            [&change](const EntityChange& player_change) {
                return player_change.entity_guid == change.entity_guid;
            });
        if (collides_with_player) {
            return invalid_state("Dedicated server player and machine replication guid collision");
        }
        auto encoded_entity = encode_machine_entity_change(change);
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
        accepted_machine_changes.push_back(change);
    }
    if (delta.chunk_snapshots.empty() && delta.removed_chunks.empty() && delta.chunks.empty() &&
        delta.entities.empty() && delta.values.empty()) {
        if (!value_changes.empty() && value_limit != 0) {
            return invalid_argument(
                "Dedicated server player delta cannot fit one value within the reliable budget");
        }
        if (!changes.empty() && entity_limit != 0) {
            return invalid_argument(
                "Dedicated server player delta cannot fit one entity within the reliable budget");
        }
        if (!machine_changes.empty() && entity_limit != 0) {
            return invalid_argument(
                "Dedicated server machine delta cannot fit one entity within the reliable budget");
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
    for (const MachineEntityChange& change : accepted_machine_changes) {
        if (change.entity.operation == GameMachineReplicationOperation::kRemove) {
            baseline->second.machines.erase(change.entity_guid.value);
        } else {
            baseline->second.machines.insert_or_assign(change.entity_guid.value,
                                                       *change.entity.machine);
        }
    }
    for (const snt::voxel::ChunkKey& key : accepted_removed_chunks) {
        baseline->second.chunks.erase(key);
    }
    for (const VisibleChunk& chunk : accepted_chunk_snapshots) {
        baseline->second.chunks.insert_or_assign(chunk.key, chunk.terrain);
    }
    for (const GameChunkDelta& chunk_delta : accepted_block_changes) {
        const auto known = baseline->second.chunks.find(chunk_delta.chunk);
        if (known == baseline->second.chunks.end()) {
            return invalid_state("Dedicated server terrain delta has no chunk baseline");
        }
        for (const GameBlockDelta& block : chunk_delta.blocks) {
            if (block.local_index >= known->second.cells.size()) {
                return invalid_state("Dedicated server terrain delta has an invalid local cell index");
            }
            known->second.cells[block.local_index] = {
                .material = block.material,
                .flags = block.flags,
            };
        }
    }
    notify_values_committed(peer, GameReplicationValueCollectionPhase::kDelta,
                            accepted_value_changes);
    ++baseline->second.next_delta_sequence;
    prune_dirty_blocks();
    return std::vector<GameReplicationMessage>{std::move(*message)};
}

void GameServerPlayerReplication::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                        std::string_view reason) noexcept {
    peer_baselines_.erase(peer.peer);
    for (IGameReplicationValueSource* source : value_sources_) {
        source->on_peer_disconnected(peer, reason);
    }
    prune_dirty_blocks();
}

void GameServerPlayerReplication::on_player_interaction(
    const GameServerPlayerInteractionEvent& event) {
    switch (event.kind) {
        case GameServerPlayerInteractionEventKind::kBlockMined:
        case GameServerPlayerInteractionEventKind::kBlockPlaced:
        case GameServerPlayerInteractionEventKind::kMachinePlaced:
            mark_block_dirty(event.command.dimension_id, event.command.block_x,
                             event.command.block_y, event.command.block_z);
            return;
        case GameServerPlayerInteractionEventKind::kBedUsed:
        case GameServerPlayerInteractionEventKind::kMachineActivated:
        case GameServerPlayerInteractionEventKind::kMachineOutputCollected:
            return;
    }
}

void GameServerPlayerReplication::on_block_physics_terrain_changed(
    const BlockPhysicsTerrainChange& change) {
    mark_block_dirty(change.dimension_id, change.block_x, change.block_y, change.block_z);
}

void GameServerPlayerReplication::on_crop_growth_terrain_changed(
    const CropGrowthTerrainChange& change) {
    mark_block_dirty(change.dimension_id, change.block_x, change.block_y, change.block_z);
}

void GameServerPlayerReplication::on_tree_growth_terrain_changed(
    const TreeGrowthTerrainChange& change) {
    mark_block_dirty(change.dimension_id, change.block_x, change.block_y, change.block_z);
}

void GameServerPlayerReplication::mark_block_dirty(std::string_view dimension_id,
                                                    int32_t block_x, int32_t block_y,
                                                    int32_t block_z) noexcept {
    if (chunks_ == nullptr || dimension_id.empty()) return;
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const snt::voxel::ChunkKey key{
        std::string(dimension_id),
        floor_divide(block_x, kChunkSize),
        floor_divide(block_y, kChunkSize),
        floor_divide(block_z, kChunkSize),
    };
    const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) {
        SNT_LOG_WARN("Committed terrain change references an absent chunk (%s %d %d %d)",
                     key.dimension_id.c_str(), key.chunk_x, key.chunk_y, key.chunk_z);
        return;
    }
    const int32_t local_x = local_coordinate(block_x, kChunkSize);
    const int32_t local_y = local_coordinate(block_y, kChunkSize);
    const int32_t local_z = local_coordinate(block_z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
        SNT_LOG_WARN("Committed terrain change has an invalid local cell (%d %d %d)",
                     local_x, local_y, local_z);
        return;
    }
    const size_t index = chunk->terrain.index_of(local_x, local_y, local_z);
    if (index >= kMaxGameBlockDeltasPerChunk) {
        SNT_LOG_WARN("Committed terrain change exceeds the replication local-index range");
        return;
    }
    const snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
    dirty_blocks_[key].insert_or_assign(
        static_cast<uint16_t>(index),
        GameBlockDelta{
            .local_index = static_cast<uint16_t>(index),
            .material = cell.material,
            .flags = cell.flags,
        });
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

snt::core::Expected<std::vector<GameServerPlayerReplication::VisibleChunk>>
GameServerPlayerReplication::visible_chunks(const GameReplicationInterest& interest) const {
    if (chunks_ == nullptr) {
        return invalid_state("Dedicated server terrain replication has no chunk registry");
    }
    if (interest.chunks.size() > config_.max_visible_chunks) {
        return invalid_argument("Dedicated server chunk interest exceeds the configured limit");
    }
    std::set<snt::voxel::ChunkKey, GameChunkKeyLess> seen;
    std::vector<VisibleChunk> visible;
    visible.reserve(interest.chunks.size());
    for (const snt::voxel::ChunkKey& key : interest.chunks) {
        if (!seen.insert(key).second) {
            return invalid_argument("Dedicated server chunk interest contains duplicate chunk keys");
        }
        const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr) {
            return invalid_state("Dedicated server chunk interest references an unloaded chunk");
        }
        auto terrain = make_game_replicated_terrain_chunk(*chunk);
        if (!terrain) return terrain.error();
        auto payload = encode_game_terrain_chunk_snapshot(*chunk);
        if (!payload) return payload.error();
        visible.push_back({
            .key = key,
            .terrain = std::move(*terrain),
            .payload = std::move(*payload),
        });
    }
    return visible;
}

snt::core::Expected<std::vector<GameServerPlayerReplication::VisibleMachine>>
GameServerPlayerReplication::visible_machines(const GameReplicationInterest& interest) const {
    if (world_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Dedicated server machine replication is unavailable");
    }
    std::set<uint64_t> requested;
    for (const snt::ecs::EntityGuid entity_guid : interest.detailed_machine_entities) {
        if (!entity_guid.valid() || !requested.insert(entity_guid.value).second) {
            return invalid_argument("Dedicated server machine interest contains duplicate or invalid guids");
        }
    }
    std::map<uint64_t, VisibleMachine> machines;
    for (const snt::voxel::ChunkKey& key : interest.chunks) {
        const GameChunkSidecar* sidecar = sidecars_->get(key);
        if (sidecar == nullptr) continue;
        for (const MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
            const snt::ecs::EntityGuid entity_guid{record.entity_guid};
            if (!requested.contains(entity_guid.value)) continue;
            const BlockEntityPlacement* anchor =
                find_machine_anchor(*sidecar, record.anchor_entity_id);
            if (anchor == nullptr) {
                return invalid_state("Dedicated server machine replication has no sidecar anchor");
            }
            const entt::entity entity = world_->find_entity_by_guid(entity_guid);
            if (entity == entt::null ||
                !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
                return invalid_state("Dedicated server machine replication has no live runtime component");
            }
            const MachineRuntimeComponent& runtime =
                world_->get_component<MachineRuntimeComponent>(entity);
            GameReplicatedMachineState state{
                .anchor_chunk = key,
                .root_x = anchor->root_x,
                .root_y = anchor->root_y,
                .root_z = anchor->root_z,
                .machine_id = runtime.machine_id,
                .max_input_slots = static_cast<uint8_t>(runtime.max_input_slots),
                .max_output_slots = static_cast<uint8_t>(runtime.max_output_slots),
                .stored_energy = runtime.stored_energy,
                .energy_capacity = runtime.energy_capacity,
                .progress_ticks = runtime.progress_ticks,
                .active_recipe_duration_ticks = runtime.active_recipe
                    ? runtime.active_recipe->duration_ticks
                    : 0,
                .run_state = static_cast<uint8_t>(runtime.state),
            };
            state.input_slots.reserve(runtime.input_slots.size());
            for (const MachineItemStack& stack : runtime.input_slots) {
                state.input_slots.push_back({.item_id = stack.item_id, .count = stack.count});
            }
            state.output_slots.reserve(runtime.output_slots.size());
            for (const MachineItemStack& stack : runtime.output_slots) {
                state.output_slots.push_back({.item_id = stack.item_id, .count = stack.count});
            }
            if (!machines.emplace(entity_guid.value,
                                  VisibleMachine{.entity_guid = entity_guid,
                                                 .state = std::move(state)})
                     .second) {
                return invalid_state("Dedicated server machine replication found duplicate runtime guids");
            }
        }
    }
    if (machines.size() != requested.size()) {
        return invalid_state("Dedicated server machine interest references an unavailable runtime");
    }
    std::vector<VisibleMachine> result;
    result.reserve(machines.size());
    for (auto& [entity_guid, machine] : machines) {
        static_cast<void>(entity_guid);
        result.push_back(std::move(machine));
    }
    return result;
}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerPlayerReplication::collect_values(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget,
    const snt::network::ReplicationTickContext& context,
    GameReplicationValueCollectionPhase phase) const {
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
        auto source_values = source->collect_values(peer, interest, budget, context, phase);
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

void GameServerPlayerReplication::notify_values_committed(
    const GameAuthenticatedPeer& peer, GameReplicationValueCollectionPhase phase,
    std::span<const GameReplicationValue> values) noexcept {
    for (IGameReplicationValueSource* source : value_sources_) {
        source->on_values_committed(peer, phase, values);
    }
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

bool GameServerPlayerReplication::same_machine_state(const GameReplicatedMachineState& left,
                                                      const GameReplicatedMachineState& right) noexcept {
    return left.anchor_chunk == right.anchor_chunk &&
           left.root_x == right.root_x && left.root_y == right.root_y &&
           left.root_z == right.root_z && left.machine_id == right.machine_id &&
           left.max_input_slots == right.max_input_slots &&
           left.max_output_slots == right.max_output_slots &&
           same_machine_stacks(left.input_slots, right.input_slots) &&
           same_machine_stacks(left.output_slots, right.output_slots) &&
           left.stored_energy == right.stored_energy &&
           left.energy_capacity == right.energy_capacity &&
           left.progress_ticks == right.progress_ticks &&
           left.active_recipe_duration_ticks == right.active_recipe_duration_ticks &&
           left.run_state == right.run_state;
}

bool GameServerPlayerReplication::same_replication_value(const GameReplicationValue& left,
                                                          const GameReplicationValue& right) noexcept {
    return left.kind == right.kind && left.operation == right.operation &&
           left.payload == right.payload;
}

void GameServerPlayerReplication::prune_dirty_blocks() noexcept {
    for (auto dirty_chunk = dirty_blocks_.begin(); dirty_chunk != dirty_blocks_.end();) {
        bool all_known_observers_match = true;
        for (const auto& [peer, baseline] : peer_baselines_) {
            static_cast<void>(peer);
            const auto known_chunk = baseline.chunks.find(dirty_chunk->first);
            if (known_chunk == baseline.chunks.end()) continue;
            for (const auto& [local_index, block] : dirty_chunk->second) {
                if (local_index >= known_chunk->second.cells.size()) {
                    all_known_observers_match = false;
                    break;
                }
                const GameReplicatedTerrainCell known = known_chunk->second.cells[local_index];
                if (known.material != block.material || known.flags != block.flags) {
                    all_known_observers_match = false;
                    break;
                }
            }
            if (!all_known_observers_match) break;
        }
        if (all_known_observers_match) {
            dirty_chunk = dirty_blocks_.erase(dirty_chunk);
        } else {
            ++dirty_chunk;
        }
    }
}

}  // namespace snt::game::replication
