// Client player-motion presentation.
//
// This module turns server-owned continuous player snapshots into local
// prediction/reconciliation and remote interpolation values. It owns no
// transport, renderer, ECS World, or persistent player state.

#pragma once

#include "core/expected.h"
#include "game/player/player_motion_simulation.h"
#include "game/player/player_replication.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace snt::game {

struct GameClientPlayerPredictionConfig {
    replication::GamePlayerMotionConfig motion;
    float reconciliation_half_life_seconds = 0.12f;
    float hard_snap_distance_blocks = 4.0f;
    size_t max_pending_inputs = 160;
};

struct GameClientPlayerPresentationState {
    std::string dimension_id;
    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_z = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float velocity_z = 0.0f;
    int16_t yaw_centidegrees = 0;
    int16_t pitch_centidegrees = 0;
    bool grounded = false;
    uint64_t source_tick = 0;
};

struct GameClientPlayerReconciliation {
    uint64_t acknowledged_movement_sequence = 0;
    size_t pending_input_count = 0;
    bool initialized = false;
    bool replayed_pending_inputs = false;
    bool hard_snapped = false;
    bool history_overflowed = false;
};

class GameClientPlayerPrediction final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameClientPlayerPrediction>> create(
        GameClientPlayerPredictionConfig config);

    GameClientPlayerPrediction(const GameClientPlayerPrediction&) = delete;
    GameClientPlayerPrediction& operator=(const GameClientPlayerPrediction&) = delete;

    // Replaces the predicted base with a server-originated state, discards
    // acknowledged movement inputs, and deterministically replays only the
    // remaining client inputs against the current presentation terrain.
    [[nodiscard]] snt::core::Expected<GameClientPlayerReconciliation> apply_authoritative(
        const replication::GameReplicatedPlayerState& state,
        const snt::player::IVoxelCollisionWorld& collision_world);

    // Predicts one client fixed tick after its typed UDP intent is queued.
    [[nodiscard]] snt::core::Expected<void> predict(
        replication::GamePlayerMovementInput input,
        const snt::player::IVoxelCollisionWorld& collision_world, float delta_seconds);

    // Presentation correction decays independently of fixed prediction so
    // render frames can remain smooth while authority stays exact.
    void advance_presentation(float delta_seconds) noexcept;

    [[nodiscard]] std::optional<GameClientPlayerPresentationState> presentation() const;
    [[nodiscard]] size_t pending_input_count() const noexcept { return pending_inputs_.size(); }
    [[nodiscard]] uint64_t last_acknowledged_movement_sequence() const noexcept {
        return last_acknowledged_movement_sequence_;
    }
    void clear() noexcept;

private:
    struct PendingInput {
        replication::GamePlayerMovementInput input;
        float delta_seconds = 0.0f;
    };

    explicit GameClientPlayerPrediction(GameClientPlayerPredictionConfig config);

    [[nodiscard]] static snt::core::Expected<replication::GamePlayerMotionState>
    motion_state_from_authoritative(const replication::GameReplicatedPlayerState& state);
    [[nodiscard]] GameClientPlayerPresentationState presentation_from_predicted() const;

    GameClientPlayerPredictionConfig config_;
    std::optional<replication::GamePlayerMotionState> predicted_state_;
    std::deque<PendingInput> pending_inputs_;
    float presentation_offset_x_ = 0.0f;
    float presentation_offset_y_ = 0.0f;
    float presentation_offset_z_ = 0.0f;
    uint64_t last_acknowledged_movement_sequence_ = 0;
    uint64_t last_predicted_movement_sequence_ = 0;
    uint64_t last_authoritative_source_tick_ = 0;
    bool history_overflowed_ = false;
};

struct GameRemotePlayerInterpolationConfig {
    uint64_t interpolation_delay_ticks = 2;
};

// The future avatar renderer consumes this cache instead of doing transport
// timing itself. Keeping it value-only makes interpolation testable before a
// remote avatar mesh/entity is introduced.
class GameRemotePlayerInterpolator final {
public:
    explicit GameRemotePlayerInterpolator(GameRemotePlayerInterpolationConfig config = {});

    void reconcile(const std::vector<replication::GameRemotePlayerState>& players,
                   uint64_t client_receive_tick);
    [[nodiscard]] std::optional<GameClientPlayerPresentationState> sample(
        snt::ecs::EntityGuid entity_guid, uint64_t client_presentation_tick) const;
    void clear() noexcept;

private:
    struct Sample {
        GameClientPlayerPresentationState state;
        uint64_t receive_tick = 0;
    };

    struct Track {
        std::optional<Sample> previous;
        Sample latest;
    };

    [[nodiscard]] static GameClientPlayerPresentationState make_presentation_state(
        const replication::GameReplicatedPlayerState& state);

    GameRemotePlayerInterpolationConfig config_;
    std::map<uint64_t, Track> tracks_;
};

}  // namespace snt::game
