// Client player-motion presentation implementation.

#include "player_motion_presentation.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] float squared_distance(float x, float y, float z) noexcept {
    return x * x + y * y + z * z;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameClientPlayerPrediction>>
GameClientPlayerPrediction::create(GameClientPlayerPredictionConfig config) {
    if (auto result = replication::validate_game_player_motion_config(config.motion); !result) {
        return result.error();
    }
    if (!finite_positive(config.reconciliation_half_life_seconds) ||
        config.reconciliation_half_life_seconds > 10.0f ||
        !finite_positive(config.hard_snap_distance_blocks) ||
        config.hard_snap_distance_blocks > 256.0f || config.max_pending_inputs == 0 ||
        config.max_pending_inputs > 4096) {
        return invalid_argument("Client player prediction configuration is invalid");
    }
    return std::unique_ptr<GameClientPlayerPrediction>(
        new GameClientPlayerPrediction(std::move(config)));
}

GameClientPlayerPrediction::GameClientPlayerPrediction(GameClientPlayerPredictionConfig config)
    : config_(std::move(config)) {}

snt::core::Expected<replication::GamePlayerMotionState>
GameClientPlayerPrediction::motion_state_from_authoritative(
    const replication::GameReplicatedPlayerState& state) {
    if (auto result = replication::validate_game_replicated_player_state(state); !result) {
        return result.error();
    }
    replication::GamePlayerMotionState motion{
        .dimension_id = state.position.dimension_id,
        .feet_x = state.motion.feet_x,
        .feet_y = state.motion.feet_y,
        .feet_z = state.motion.feet_z,
        .velocity_x = state.motion.velocity_x,
        .velocity_y = state.motion.velocity_y,
        .velocity_z = state.motion.velocity_z,
        .yaw_centidegrees = state.motion.yaw_centidegrees,
        .pitch_centidegrees = state.motion.pitch_centidegrees,
        .last_processed_input_sequence = state.motion.last_processed_input_sequence,
        .grounded = state.motion.grounded,
    };
    if (auto result = replication::validate_game_player_motion_state(motion); !result) {
        return result.error();
    }
    return motion;
}

snt::core::Expected<GameClientPlayerReconciliation>
GameClientPlayerPrediction::apply_authoritative(
    const replication::GameReplicatedPlayerState& state,
    const snt::player::IVoxelCollisionWorld& collision_world) {
    auto authoritative = motion_state_from_authoritative(state);
    if (!authoritative) return authoritative.error();

    GameClientPlayerReconciliation report{
        .acknowledged_movement_sequence = authoritative->last_processed_input_sequence,
        .history_overflowed = std::exchange(history_overflowed_, false),
    };
    if (predicted_state_.has_value() &&
        authoritative->dimension_id == predicted_state_->dimension_id &&
        last_authoritative_source_tick_ != 0 &&
        state.motion.source_tick <= last_authoritative_source_tick_) {
        report.acknowledged_movement_sequence = last_acknowledged_movement_sequence_;
        report.pending_input_count = pending_inputs_.size();
        report.initialized = true;
        return report;
    }
    if (predicted_state_.has_value() &&
        authoritative->dimension_id == predicted_state_->dimension_id &&
        authoritative->last_processed_input_sequence < last_acknowledged_movement_sequence_) {
        report.pending_input_count = pending_inputs_.size();
        report.initialized = true;
        return report;
    }

    const std::optional<GameClientPlayerPresentationState> before = presentation();
    const bool dimension_changed = predicted_state_.has_value() &&
        predicted_state_->dimension_id != authoritative->dimension_id;
    if (dimension_changed) {
        pending_inputs_.clear();
        presentation_offset_x_ = 0.0f;
        presentation_offset_y_ = 0.0f;
        presentation_offset_z_ = 0.0f;
        last_predicted_movement_sequence_ = 0;
        last_acknowledged_movement_sequence_ = 0;
    }

    last_acknowledged_movement_sequence_ = authoritative->last_processed_input_sequence;
    last_authoritative_source_tick_ = state.motion.source_tick;
    std::erase_if(pending_inputs_, [ack = last_acknowledged_movement_sequence_](const PendingInput& input) {
        return input.input.client_sequence <= ack;
    });
    predicted_state_ = std::move(*authoritative);

    for (const PendingInput& pending : pending_inputs_) {
        auto stepped = replication::advance_game_player_motion(
            *predicted_state_, pending.input, collision_world, config_.motion,
            pending.delta_seconds);
        if (!stepped) return stepped.error();
        report.replayed_pending_inputs = true;
    }

    if (before.has_value() && !dimension_changed) {
        const float correction_x = before->feet_x - predicted_state_->feet_x;
        const float correction_y = before->feet_y - predicted_state_->feet_y;
        const float correction_z = before->feet_z - predicted_state_->feet_z;
        if (squared_distance(correction_x, correction_y, correction_z) >
            config_.hard_snap_distance_blocks * config_.hard_snap_distance_blocks) {
            presentation_offset_x_ = 0.0f;
            presentation_offset_y_ = 0.0f;
            presentation_offset_z_ = 0.0f;
            report.hard_snapped = true;
        } else {
            presentation_offset_x_ = correction_x;
            presentation_offset_y_ = correction_y;
            presentation_offset_z_ = correction_z;
        }
    }
    report.pending_input_count = pending_inputs_.size();
    report.initialized = true;
    return report;
}

snt::core::Expected<void> GameClientPlayerPrediction::predict(
    replication::GamePlayerMovementInput input,
    const snt::player::IVoxelCollisionWorld& collision_world, float delta_seconds) {
    if (!predicted_state_.has_value()) return {};
    if (auto result = replication::validate_game_player_movement_input(input); !result) {
        return result.error();
    }
    if (input.client_sequence <= last_predicted_movement_sequence_) {
        return invalid_state("Client predicted movement sequence did not increase strictly");
    }
    auto stepped = replication::advance_game_player_motion(
        *predicted_state_, input, collision_world, config_.motion, delta_seconds);
    if (!stepped) return stepped.error();
    pending_inputs_.push_back({.input = input, .delta_seconds = delta_seconds});
    while (pending_inputs_.size() > config_.max_pending_inputs) {
        pending_inputs_.pop_front();
        history_overflowed_ = true;
    }
    last_predicted_movement_sequence_ = input.client_sequence;
    return {};
}

void GameClientPlayerPrediction::advance_presentation(float delta_seconds) noexcept {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f) return;
    const float decay = std::exp2(-delta_seconds / config_.reconciliation_half_life_seconds);
    presentation_offset_x_ *= decay;
    presentation_offset_y_ *= decay;
    presentation_offset_z_ *= decay;
}

std::optional<GameClientPlayerPresentationState> GameClientPlayerPrediction::presentation() const {
    if (!predicted_state_.has_value()) return std::nullopt;
    return presentation_from_predicted();
}

GameClientPlayerPresentationState GameClientPlayerPrediction::presentation_from_predicted() const {
    return {
        .dimension_id = predicted_state_->dimension_id,
        .feet_x = predicted_state_->feet_x + presentation_offset_x_,
        .feet_y = predicted_state_->feet_y + presentation_offset_y_,
        .feet_z = predicted_state_->feet_z + presentation_offset_z_,
        .velocity_x = predicted_state_->velocity_x,
        .velocity_y = predicted_state_->velocity_y,
        .velocity_z = predicted_state_->velocity_z,
        .yaw_centidegrees = predicted_state_->yaw_centidegrees,
        .pitch_centidegrees = predicted_state_->pitch_centidegrees,
        .grounded = predicted_state_->grounded,
        .source_tick = last_authoritative_source_tick_,
    };
}

void GameClientPlayerPrediction::clear() noexcept {
    predicted_state_.reset();
    pending_inputs_.clear();
    presentation_offset_x_ = 0.0f;
    presentation_offset_y_ = 0.0f;
    presentation_offset_z_ = 0.0f;
    last_acknowledged_movement_sequence_ = 0;
    last_predicted_movement_sequence_ = 0;
    last_authoritative_source_tick_ = 0;
    history_overflowed_ = false;
}

GameRemotePlayerInterpolator::GameRemotePlayerInterpolator(
    GameRemotePlayerInterpolationConfig config)
    : config_(config) {}

GameClientPlayerPresentationState GameRemotePlayerInterpolator::make_presentation_state(
    const replication::GameReplicatedPlayerState& state) {
    return {
        .dimension_id = state.position.dimension_id,
        .feet_x = state.motion.feet_x,
        .feet_y = state.motion.feet_y,
        .feet_z = state.motion.feet_z,
        .velocity_x = state.motion.velocity_x,
        .velocity_y = state.motion.velocity_y,
        .velocity_z = state.motion.velocity_z,
        .yaw_centidegrees = state.motion.yaw_centidegrees,
        .pitch_centidegrees = state.motion.pitch_centidegrees,
        .grounded = state.motion.grounded,
        .source_tick = state.motion.source_tick,
    };
}

void GameRemotePlayerInterpolator::reconcile(
    const std::vector<replication::GameRemotePlayerState>& players,
    uint64_t client_receive_tick) {
    std::set<uint64_t> active;
    for (const replication::GameRemotePlayerState& player : players) {
        if (!player.entity_guid.valid()) continue;
        active.insert(player.entity_guid.value);
        const GameClientPlayerPresentationState next = make_presentation_state(player.player);
        const auto track = tracks_.find(player.entity_guid.value);
        if (track == tracks_.end()) {
            tracks_.emplace(player.entity_guid.value, Track{
                .latest = {.state = next, .receive_tick = client_receive_tick},
            });
            continue;
        }
        if (next.source_tick < track->second.latest.state.source_tick ||
            (next.source_tick == track->second.latest.state.source_tick &&
             next.dimension_id == track->second.latest.state.dimension_id &&
             next.feet_x == track->second.latest.state.feet_x &&
             next.feet_y == track->second.latest.state.feet_y &&
             next.feet_z == track->second.latest.state.feet_z)) {
            continue;
        }
        track->second.previous = track->second.latest;
        track->second.latest = {.state = next, .receive_tick = client_receive_tick};
    }
    std::erase_if(tracks_, [&active](const auto& entry) { return !active.contains(entry.first); });
}

std::optional<GameClientPlayerPresentationState> GameRemotePlayerInterpolator::sample(
    snt::ecs::EntityGuid entity_guid, uint64_t client_presentation_tick) const {
    if (!entity_guid.valid()) return std::nullopt;
    const auto track = tracks_.find(entity_guid.value);
    if (track == tracks_.end()) return std::nullopt;
    if (!track->second.previous.has_value()) return track->second.latest.state;

    const Sample& previous = *track->second.previous;
    const Sample& latest = track->second.latest;
    if (previous.state.dimension_id != latest.state.dimension_id ||
        latest.receive_tick <= previous.receive_tick) {
        return latest.state;
    }
    const uint64_t target_tick = client_presentation_tick > config_.interpolation_delay_ticks
        ? client_presentation_tick - config_.interpolation_delay_ticks
        : 0;
    if (target_tick <= previous.receive_tick) return previous.state;
    if (target_tick >= latest.receive_tick) return latest.state;
    const float alpha = static_cast<float>(target_tick - previous.receive_tick) /
                        static_cast<float>(latest.receive_tick - previous.receive_tick);
    GameClientPlayerPresentationState result = latest.state;
    result.feet_x = previous.state.feet_x + (latest.state.feet_x - previous.state.feet_x) * alpha;
    result.feet_y = previous.state.feet_y + (latest.state.feet_y - previous.state.feet_y) * alpha;
    result.feet_z = previous.state.feet_z + (latest.state.feet_z - previous.state.feet_z) * alpha;
    result.velocity_x = previous.state.velocity_x +
                        (latest.state.velocity_x - previous.state.velocity_x) * alpha;
    result.velocity_y = previous.state.velocity_y +
                        (latest.state.velocity_y - previous.state.velocity_y) * alpha;
    result.velocity_z = previous.state.velocity_z +
                        (latest.state.velocity_z - previous.state.velocity_z) * alpha;
    return result;
}

void GameRemotePlayerInterpolator::clear() noexcept {
    tracks_.clear();
}

}  // namespace snt::game
