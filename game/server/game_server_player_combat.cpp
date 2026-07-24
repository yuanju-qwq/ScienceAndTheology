// Dedicated-server player combat and death producer implementation.

#define SNT_LOG_CHANNEL "game.server_player_combat"
#include "game/server/game_server_player_combat.h"

#include "core/error.h"
#include "core/log.h"
#include "game/server/game_server_player_state.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] const char* damage_source_name(GameServerPlayerDamageSource source) noexcept {
    switch (source) {
        case GameServerPlayerDamageSource::kWildCreature: return "wild creature";
        case GameServerPlayerDamageSource::kEnvironment: return "environment";
        case GameServerPlayerDamageSource::kSourceLaw: return "source law";
        case GameServerPlayerDamageSource::kSystem: return "system";
    }
    return "unknown";
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerCombatService>>
GameServerPlayerCombatService::create(GameServerPlayerState& player_state,
                                      GameServerPlayerDeathService& death_service) {
    return std::unique_ptr<GameServerPlayerCombatService>(
        new GameServerPlayerCombatService(player_state, death_service));
}

GameServerPlayerCombatService::GameServerPlayerCombatService(
    GameServerPlayerState& player_state, GameServerPlayerDeathService& death_service) noexcept
    : player_state_(&player_state), death_service_(&death_service) {}

snt::core::Expected<GameServerPlayerDamageResult> GameServerPlayerCombatService::apply_damage(
    const GameAuthenticatedPeer& peer, const GameServerPlayerDamageRequest& request,
    uint64_t source_tick) {
    if (player_state_ == nullptr || death_service_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Player combat service has no authoritative server services"};
    }
    if (!std::isfinite(request.damage) || request.damage <= 0.0f) {
        return invalid_argument("Player combat damage must be finite and positive");
    }
    auto before = player_state_->combat_state_for_peer(peer);
    if (!before) return before.error();

    if (active_damage_tick_.has_value() && source_tick < *active_damage_tick_) {
        return invalid_argument("Player combat damage source tick moved backwards");
    }
    if (!active_damage_tick_.has_value() || source_tick > *active_damage_tick_) {
        active_damage_tick_ = source_tick;
        accounts_dead_this_tick_.clear();
    }
    if (accounts_dead_this_tick_.contains(peer.identity.account_id)) {
        return GameServerPlayerDamageResult{
            .combat = std::move(*before),
            .ignored_after_death_this_tick = true,
        };
    }

    const float applied_damage = std::min(request.damage, before->health_current);
    if (request.damage < before->health_current) {
        if (auto result = player_state_->apply_nonlethal_combat_damage(peer, request.damage);
            !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerCombatService::apply_damage(nonlethal)");
            return error;
        }
        auto after = player_state_->combat_state_for_peer(peer);
        if (!after) return after.error();
        return GameServerPlayerDamageResult{
            .damage_applied = applied_damage,
            .combat = std::move(*after),
        };
    }

    // Resolve the durable world/inventory transaction before changing health.
    // A failed grave or respawn preflight must leave the player alive with its
    // prior health rather than creating a zero-health half-death state.
    auto death = death_service_->resolve_death(peer, source_tick);
    if (!death) {
        auto error = death.error();
        error.with_context("GameServerPlayerCombatService::apply_damage(resolve death)");
        return error;
    }
    if (auto result = player_state_->restore_full_combat_health(peer); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerCombatService::apply_damage(restore health)");
        return error;
    }
    auto after = player_state_->combat_state_for_peer(peer);
    if (!after) return after.error();
    accounts_dead_this_tick_.insert(peer.identity.account_id);
    SNT_LOG_INFO("Resolved combat death for player '%s' from %s (entity=%llu species=%u)",
                 peer.identity.account_id.c_str(), damage_source_name(request.source),
                 static_cast<unsigned long long>(request.source_entity_id),
                 static_cast<unsigned>(request.source_species_id));
    return GameServerPlayerDamageResult{
        .damage_applied = applied_damage,
        .combat = std::move(*after),
        .death_resolved = true,
    };
}

std::vector<GameWildCreaturePlayerTarget>
GameServerPlayerCombatService::active_wild_creature_player_targets() const {
    if (player_state_ == nullptr) return {};
    auto snapshots = player_state_->active_player_snapshots();
    if (!snapshots) {
        if (!target_snapshot_error_logged_) {
            SNT_LOG_WARN("Unable to collect active player targets for wild creatures: %s",
                         snapshots.error().format().c_str());
            target_snapshot_error_logged_ = true;
        }
        return {};
    }
    target_snapshot_error_logged_ = false;
    std::vector<GameWildCreaturePlayerTarget> targets;
    targets.reserve(snapshots->size());
    for (const GameServerPlayerSnapshot& snapshot : *snapshots) {
        if (snapshot.account_id.empty() || snapshot.position.dimension_id.empty()) continue;
        targets.push_back({
            .account_id = snapshot.account_id,
            .dimension_id = snapshot.position.dimension_id,
            .feet_x = static_cast<float>(snapshot.position.position.x),
            .feet_y = static_cast<float>(snapshot.position.position.y),
            .feet_z = static_cast<float>(snapshot.position.position.z),
        });
    }
    return targets;
}

snt::core::Expected<void> GameServerPlayerCombatService::apply_wild_creature_player_damage(
    const GameWildCreaturePlayerDamageRequest& request) {
    if (player_state_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Player combat service has no authoritative player state"};
    }
    if (request.target_account_id.empty() || request.wild_entity_id == 0 ||
        request.species_id == 0 || !std::isfinite(request.damage) || request.damage <= 0.0f) {
        return invalid_argument("Wild creature player damage request is invalid");
    }
    auto peer = player_state_->active_peer_for_account(request.target_account_id);
    if (!peer) return peer.error();
    auto result = apply_damage(
        *peer,
        {
            .damage = request.damage,
            .source = GameServerPlayerDamageSource::kWildCreature,
            .source_entity_id = request.wild_entity_id,
            .source_species_id = request.species_id,
        },
        request.source_tick);
    if (!result) return result.error();
    return {};
}

}  // namespace snt::game::replication
