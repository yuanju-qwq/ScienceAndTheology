// Dedicated-server authority for native creature actions.
//
// This module consumes typed creature commands after transport admission. It
// derives damage, reach, inventory consumption, and enclosure geometry from
// server-owned state; clients submit only stable target ids and a feed item.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/simulation/wild_creature_system.h"

#include <cstdint>
#include <memory>

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game {
class GameContentRegistry;
}

namespace snt::game::replication {

class GameServerPlayerState;

// Content can replace the generic terrain enclosure search once native fence
// topology has an explicit material/tag model. The validator remains outside
// the wildlife system so persistence and population simulation stay unaware
// of server/player authority rules.
class IGameServerCreaturePenValidator {
public:
    virtual ~IGameServerCreaturePenValidator() = default;

    [[nodiscard]] virtual snt::core::Expected<GameCreaturePenBounds>
    validate_pen(const GameCreaturePresentationState& creature,
                 snt::voxel::ChunkRegistry& chunks) const = 0;
};

class IGameServerCreatureInteractionService {
public:
    virtual ~IGameServerCreatureInteractionService() = default;

    [[nodiscard]] virtual snt::core::Expected<void> attack_creature(
        const GameAuthenticatedPeer& peer, const GameCreatureAttackCommand& command,
        uint64_t source_tick) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> capture_creature(
        const GameAuthenticatedPeer& peer, const GameCreatureCaptureCommand& command,
        uint64_t source_tick) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> feed_captive_creature(
        const GameAuthenticatedPeer& peer, const GameCaptiveCreatureFeedCommand& command,
        uint64_t source_tick) = 0;
};

struct GameServerCreatureInteractionConfig {
    float unarmed_damage = 0.25f;
    float feed_tame_progress = 0.25f;
    uint32_t terrain_pen_search_radius_blocks = 8;
    uint32_t terrain_pen_max_cells = 256;
};

class GameServerCreatureInteractionService final
    : public IGameServerCreatureInteractionService {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerCreatureInteractionService>>
    create(GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
           const GameContentRegistry& content, GameWildCreatureSystem& wildlife,
           GameServerCreatureInteractionConfig config = {},
           const IGameServerCreaturePenValidator* pen_validator = nullptr);

    GameServerCreatureInteractionService(const GameServerCreatureInteractionService&) = delete;
    GameServerCreatureInteractionService& operator=(
        const GameServerCreatureInteractionService&) = delete;

    [[nodiscard]] snt::core::Expected<void> attack_creature(
        const GameAuthenticatedPeer& peer, const GameCreatureAttackCommand& command,
        uint64_t source_tick) override;
    [[nodiscard]] snt::core::Expected<void> capture_creature(
        const GameAuthenticatedPeer& peer, const GameCreatureCaptureCommand& command,
        uint64_t source_tick) override;
    [[nodiscard]] snt::core::Expected<void> feed_captive_creature(
        const GameAuthenticatedPeer& peer, const GameCaptiveCreatureFeedCommand& command,
        uint64_t source_tick) override;

private:
    GameServerCreatureInteractionService(
        GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
        const GameContentRegistry& content, GameWildCreatureSystem& wildlife,
        GameServerCreatureInteractionConfig config,
        const IGameServerCreaturePenValidator* pen_validator) noexcept;

    [[nodiscard]] snt::core::Expected<void> validate_reach(
        const GameAuthenticatedPeer& peer,
        const GameCreaturePresentationState& creature) const;
    [[nodiscard]] snt::core::Expected<GameCreaturePenBounds> validate_capture_pen(
        const GameCreaturePresentationState& creature) const;
    [[nodiscard]] snt::core::Expected<GameCreaturePenBounds> validate_terrain_pen(
        const GameCreaturePresentationState& creature) const;
    [[nodiscard]] float attack_damage_for(const GameAuthenticatedPeer& peer) const;

    GameServerPlayerState* player_state_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    const GameContentRegistry* content_ = nullptr;
    GameWildCreatureSystem* wildlife_ = nullptr;
    GameServerCreatureInteractionConfig config_;
    const IGameServerCreaturePenValidator* pen_validator_ = nullptr;
};

}  // namespace snt::game::replication
