// Dedicated-server player combat and death producer.
//
// This module is the sole bridge from trusted server-side damage events to
// online player health and the existing grave/respawn transaction. It accepts
// no client command payloads: wildlife, environment, and future source-law
// systems use its narrow value interface after they have made their own
// authoritative simulation decision.

#pragma once

#include "core/expected.h"
#include "game/server/game_server_player_death.h"
#include "game/simulation/wild_creature_system.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace snt::game::replication {

class GameServerPlayerState;

enum class GameServerPlayerDamageSource : uint8_t {
    kWildCreature = 0,
    kEnvironment,
    kSourceLaw,
    kSystem,
};

struct GameServerPlayerDamageRequest {
    float damage = 0.0f;
    GameServerPlayerDamageSource source = GameServerPlayerDamageSource::kSystem;
    uint64_t source_entity_id = 0;
    uint16_t source_species_id = 0;
};

struct GameServerPlayerDamageResult {
    float damage_applied = 0.0f;
    GamePlayerCombatState combat;
    bool death_resolved = false;
    // The wildlife system intentionally shares a start-of-tick player
    // snapshot among nearby predators. Once a death has committed, later
    // damage from that same authoritative tick must not target the newly
    // respawned actor using the stale position value.
    bool ignored_after_death_this_tick = false;
};

class GameServerPlayerCombatService final
    : public IGameWildCreaturePlayerTargetProvider,
      public IGameWildCreaturePlayerDamageSink {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerCombatService>>
    create(GameServerPlayerState& player_state, GameServerPlayerDeathService& death_service);

    GameServerPlayerCombatService(const GameServerPlayerCombatService&) = delete;
    GameServerPlayerCombatService& operator=(const GameServerPlayerCombatService&) = delete;

    [[nodiscard]] snt::core::Expected<GameServerPlayerDamageResult> apply_damage(
        const GameAuthenticatedPeer& peer, const GameServerPlayerDamageRequest& request,
        uint64_t source_tick);

    [[nodiscard]] std::vector<GameWildCreaturePlayerTarget>
    active_wild_creature_player_targets() const override;
    [[nodiscard]] snt::core::Expected<void> apply_wild_creature_player_damage(
        const GameWildCreaturePlayerDamageRequest& request) override;

private:
    GameServerPlayerCombatService(GameServerPlayerState& player_state,
                                  GameServerPlayerDeathService& death_service) noexcept;

    GameServerPlayerState* player_state_ = nullptr;
    GameServerPlayerDeathService* death_service_ = nullptr;
    mutable bool target_snapshot_error_logged_ = false;
    // All damage producers run on the server main fixed tick. Keep only the
    // current tick's resolved deaths, so the guard has bounded lifetime while
    // preventing stale producer snapshots from causing a second death.
    std::optional<uint64_t> active_damage_tick_;
    std::set<std::string, std::less<>> accounts_dead_this_tick_;
};

}  // namespace snt::game::replication
