// Dedicated-server authoritative player movement implementation.

#define SNT_LOG_CHANNEL "game.server_player_movement"
#include "game/server/game_server_player_movement.h"

#include "core/error.h"
#include "core/log.h"
#include "game/server/game_server_player_state.h"
#include "player/voxel_collision.h"
#include "voxel/data/chunk_registry.h"

#include <algorithm>
#include <cmath>
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

[[nodiscard]] bool same_position(const snt::ecs::Position& left,
                                  const snt::ecs::Position& right) noexcept {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] GameAuthenticatedPeer peer_for_snapshot(
    const GameServerPlayerSnapshot& snapshot) {
    return {
        .peer = snapshot.peer,
        .identity = {
            .provider = snapshot.identity_provider,
            .account_id = snapshot.account_id,
            .display_name = snapshot.display_name,
        },
    };
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerMovement>> GameServerPlayerMovement::create(
    GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
    GameServerPlayerMovementConfig config) {
    if (auto result = validate_game_player_motion_config(config.motion); !result) return result.error();
    return std::unique_ptr<GameServerPlayerMovement>(
        new GameServerPlayerMovement(player_state, chunks, std::move(config)));
}

GameServerPlayerMovement::GameServerPlayerMovement(
    GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
    GameServerPlayerMovementConfig config)
    : player_state_(&player_state), chunks_(&chunks), config_(std::move(config)) {}

snt::core::Expected<void> GameServerPlayerMovement::enqueue_player_movement_input(
    const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
    const snt::network::ReplicationTickContext& context) {
    if (player_state_ == nullptr || chunks_ == nullptr) {
        return invalid_state("Dedicated server player movement has no world services");
    }
    if (auto result = validate_game_player_movement_input(input); !result) return result.error();
    if (auto snapshot = player_state_->snapshot_for_peer(peer); !snapshot) {
        return snapshot.error();
    }
    inputs_.insert_or_assign(peer.peer, TimedInput{
        .input = std::move(input),
        .received_tick = context.tick_index,
    });
    return {};
}

void GameServerPlayerMovement::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                     std::string_view) noexcept {
    if (peer.peer != snt::network::kInvalidPeerId) inputs_.erase(peer.peer);
}

snt::core::Expected<void> GameServerPlayerMovement::reset_player_motion(
    const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) {
        return invalid_state("Dedicated server player movement has no player state");
    }
    auto snapshot = player_state_->snapshot_for_peer(peer);
    if (!snapshot) return snapshot.error();
    motions_.erase(snapshot->entity_guid.value);
    motion_peers_.erase(snapshot->entity_guid.value);
    inputs_.erase(peer.peer);
    return {};
}

snt::core::Expected<void> GameServerPlayerMovement::tick(
    const snt::network::ReplicationTickContext& context) {
    if (player_state_ == nullptr || chunks_ == nullptr) {
        return invalid_state("Dedicated server player movement has no world services");
    }
    if (!std::isfinite(context.delta_seconds) || context.delta_seconds <= 0.0f ||
        context.delta_seconds > 1.0f) {
        return invalid_argument("Dedicated server player movement tick delta is invalid");
    }

    auto snapshots = player_state_->active_player_snapshots();
    if (!snapshots) return snapshots.error();

    std::set<uint64_t> active_entities;
    for (const GameServerPlayerSnapshot& snapshot : *snapshots) {
        if (!snapshot.entity_guid.valid() || snapshot.peer == snt::network::kInvalidPeerId) {
            return invalid_state("Dedicated server movement observed an invalid active player snapshot");
        }
        active_entities.insert(snapshot.entity_guid.value);

        GamePlayerMotionState& state = motions_[snapshot.entity_guid.value];
        const auto known_motion_peer = motion_peers_.find(snapshot.entity_guid.value);
        const bool peer_changed = known_motion_peer == motion_peers_.end() ||
            known_motion_peer->second != snapshot.peer;
        const bool needs_reset = state.dimension_id != snapshot.position.dimension_id ||
            !same_position(game_player_motion_world_position(state).position,
                           snapshot.position.position);
        if (needs_reset) {
            state = make_game_player_motion_state(snapshot.position);
        } else if (peer_changed) {
            // Preserve physical state across a takeover, while moving ACK and
            // one-shot jump tracking into the replacement peer's sequence space.
            state.last_processed_input_sequence = 0;
            state.last_consumed_jump_sequence = 0;
            SNT_LOG_INFO("Reset movement acknowledgement for entity %llu after peer change %llu -> %llu",
                         static_cast<unsigned long long>(snapshot.entity_guid.value),
                         static_cast<unsigned long long>(
                             known_motion_peer == motion_peers_.end()
                                 ? snt::network::kInvalidPeerId
                                 : known_motion_peer->second),
                         static_cast<unsigned long long>(snapshot.peer));
        }
        if (peer_changed && known_motion_peer != motion_peers_.end()) {
            inputs_.erase(known_motion_peer->second);
        }
        motion_peers_.insert_or_assign(snapshot.entity_guid.value, snapshot.peer);

        const auto input = inputs_.find(snapshot.peer);
        const bool has_fresh_input = input != inputs_.end() &&
            context.tick_index >= input->second.received_tick &&
            context.tick_index - input->second.received_tick <= config_.motion.input_timeout_ticks;
        const snt::player::CollisionWorldView collision_world(
            chunks_, snapshot.position.dimension_id, config_.missing_chunks_are_solid);
        auto stepped = advance_game_player_motion(
            state,
            has_fresh_input ? std::optional<GamePlayerMovementInput>(input->second.input)
                            : std::nullopt,
            collision_world, config_.motion, context.delta_seconds);
        if (!stepped) {
            auto error = stepped.error();
            error.with_context("GameServerPlayerMovement::tick(shared motion step)");
            return error;
        }

        GamePlayerWorldPosition next_position = game_player_motion_world_position(state);
        if (!same_position(next_position.position, snapshot.position.position)) {
            auto result = player_state_->set_authoritative_position(
                peer_for_snapshot(snapshot), std::move(next_position));
            if (!result) {
                auto error = result.error();
                error.with_context("GameServerPlayerMovement::tick(update position)");
                return error;
            }
        }
    }

    std::erase_if(motions_, [&active_entities](const auto& entry) {
        return !active_entities.contains(entry.first);
    });
    std::erase_if(motion_peers_, [&active_entities](const auto& entry) {
        return !active_entities.contains(entry.first);
    });
    std::erase_if(inputs_, [&snapshots](const auto& entry) {
        return std::none_of(snapshots->begin(), snapshots->end(), [&entry](const auto& snapshot) {
            return snapshot.peer == entry.first;
        });
    });
    return {};
}

std::optional<GamePlayerMotionState> GameServerPlayerMovement::motion_snapshot_for_player(
    snt::ecs::EntityGuid entity_guid) const {
    if (!entity_guid.valid()) return std::nullopt;
    const auto found = motions_.find(entity_guid.value);
    if (found == motions_.end()) return std::nullopt;
    return found->second;
}

}  // namespace snt::game::replication
