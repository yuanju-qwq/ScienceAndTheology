// Near-field wild creature projection and interaction system.
//
// Ownership: GameEcosystemSystem owns persistent aggregate wild population.
// This module owns only temporary interactive wild representatives and writes
// durable CaptiveCreature values through GameChunkSidecarRegistry after a
// confirmed capture. It contains no Godot, renderer, network, or ECS handles.

#pragma once

#include "core/expected.h"
#include "game/simulation/ecosystem_system.h"
#include "game/world/defs/creature_presentation.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace snt::game {

// Value supplied only after an authoritative interaction service has checked
// enclosure geometry and player reach. The wildlife system revalidates bounds
// and creature position but never accepts an untrusted client enclosure claim.
struct GameCreaturePenBounds {
    int32_t min_x = 0;
    int32_t min_y = 0;
    int32_t min_z = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;
    int32_t max_z = 0;

    [[nodiscard]] bool valid() const noexcept {
        return min_x <= max_x && min_y <= max_y && min_z <= max_z;
    }
};

struct GameWildCreatureCaptureRequest {
    uint64_t wild_entity_id = 0;
    ChunkKey captive_chunk;
    GameCreaturePenBounds pen_bounds;
    float initial_tame_progress = 0.0f;
};

struct GameWildCreatureCaptureResult {
    bool captured = false;
    uint64_t captive_entity_id = 0;
    uint16_t species_id = 0;
    ChunkKey captive_chunk;
};

struct GameWildCreatureAttackResult {
    bool hit = false;
    bool killed = false;
    uint64_t wild_entity_id = 0;
    uint16_t species_id = 0;
    float damage_dealt = 0.0f;
    float remaining_health = 0.0f;
    ChunkKey chunk;
};

// The wildlife simulation sees players only as immutable position values. It
// never imports player ECS components, transport peers, or persistence state.
struct GameWildCreaturePlayerTarget {
    std::string account_id;
    std::string dimension_id;
    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_z = 0.0f;
};

class IGameWildCreaturePlayerTargetProvider {
public:
    virtual ~IGameWildCreaturePlayerTargetProvider() = default;

    [[nodiscard]] virtual std::vector<GameWildCreaturePlayerTarget>
    active_wild_creature_player_targets() const = 0;
};

// A predator attack becomes a value event at the simulation/server boundary.
// The sink owns health, death, grave, and respawn transactions; wildlife only
// owns deterministic target selection and cooldown timing.
struct GameWildCreaturePlayerDamageRequest {
    uint64_t wild_entity_id = 0;
    uint16_t species_id = 0;
    std::string target_account_id;
    float damage = 0.0f;
    uint64_t source_tick = 0;
};

class IGameWildCreaturePlayerDamageSink {
public:
    virtual ~IGameWildCreaturePlayerDamageSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> apply_wild_creature_player_damage(
        const GameWildCreaturePlayerDamageRequest& request) = 0;
};

struct GameCaptiveCreatureFeedResult {
    bool fed = false;
    bool became_tamed = false;
    bool breeding_started = false;
    uint64_t captive_entity_id = 0;
    uint64_t partner_entity_id = 0;
    uint16_t species_id = 0;
    float tame_progress = 0.0f;
    uint64_t gestation_end_tick = 0;
    ChunkKey chunk;
};

// Current-format captive husbandry tuning. All durations use authoritative
// simulation ticks; neither wall-clock time nor client-side timers decide a
// birth, maturity transition, or movement target.
struct GameWildCreatureConfig {
    uint32_t max_captive_creatures_per_chunk = 64;
    uint64_t killed_proxy_suppression_ticks = 120;
    uint64_t captured_proxy_suppression_ticks = 300;
    // Temporary wild representatives exist only inside the interactive
    // ecology circle. Their behavior is deterministic and is discarded when
    // that circle unloads, unlike durable captive creature state below.
    uint64_t wild_wander_interval_ticks = 60;
    uint32_t wild_wander_target_attempts = 16;
    float wild_fallback_move_speed = 0.06f;
    float wild_fallback_flee_detection_radius = 8.0f;
    uint64_t wild_presentation_interval_ticks = 5;
    // Zero disables autonomous wandering. A nonzero interval schedules a
    // deterministic target search inside the verified pen bounds.
    uint64_t captive_wander_interval_ticks = 80;
    uint32_t captive_wander_target_attempts = 16;
    // Used only when a content species does not define a positive move speed.
    float captive_fallback_move_speed = 0.06f;
    uint64_t captive_growth_ticks = 24000;
    uint64_t captive_gestation_ticks = 2400;
    uint64_t captive_breed_cooldown_ticks = 12000;
    // Zero publishes every authoritative movement update. A positive cadence
    // limits presentation traffic while authoritative state still advances
    // every ecosystem tick.
    uint64_t captive_presentation_interval_ticks = 5;
};

class GameWildCreatureSystem final : public IGameEcosystemWildProxySink,
                                      public IGameEcosystemFarVisualSink,
                                      public IGameEcosystemCaptiveLifecycleSink {
public:
    GameWildCreatureSystem(GameEcosystemSystem& ecosystem,
                           ChunkRegistry& chunks,
                           GameChunkSidecarRegistry& sidecars,
                           const CreatureSpeciesRegistry& species_catalog,
                           GameWildCreatureConfig config = {}) noexcept;

    GameWildCreatureSystem(const GameWildCreatureSystem&) = delete;
    GameWildCreatureSystem& operator=(const GameWildCreatureSystem&) = delete;

    void set_presentation_sink(IGameCreaturePresentationSink* sink) noexcept {
        presentation_sink_ = sink;
    }
    void set_player_target_provider(
        const IGameWildCreaturePlayerTargetProvider* provider) noexcept {
        player_target_provider_ = provider;
        player_target_cache_tick_.reset();
    }
    void set_player_damage_sink(IGameWildCreaturePlayerDamageSink* sink) noexcept {
        player_damage_sink_ = sink;
    }

    void request_wild_proxy_rebalance(
        const GameEcosystemWildProxyRebalanceRequest& request) override;
    void tick_interactive_wild_creatures(
        const GameEcosystemWildTickRequest& request) override;
    void request_far_visual_rebalance(
        const GameEcosystemFarVisualRebalanceRequest& request) override;
    void tick_captive_creatures(const GameEcosystemCaptiveTickRequest& request) override;

    [[nodiscard]] std::optional<GameCreaturePresentationState> find_wild_creature(
        uint64_t wild_entity_id) const;
    [[nodiscard]] std::optional<GameCreaturePresentationState> find_captive_creature(
        uint64_t captive_entity_id) const;
    [[nodiscard]] size_t wild_creature_count() const noexcept { return wild_creatures_.size(); }
    [[nodiscard]] size_t far_visual_creature_count() const noexcept {
        return far_visual_creatures_.size();
    }
    [[nodiscard]] const CreatureSpeciesRegistry& species_catalog() const noexcept {
        return *species_catalog_;
    }

    // Lower-level authority operations. Player-interaction systems perform
    // target selection/reach/enclosure checks before calling these methods.
    [[nodiscard]] GameWildCreatureAttackResult apply_damage(
        uint64_t wild_entity_id, float damage, uint64_t source_tick);
    [[nodiscard]] snt::core::Expected<GameWildCreatureCaptureResult> capture_wild_creature(
        const GameWildCreatureCaptureRequest& request, uint64_t source_tick);
    [[nodiscard]] snt::core::Expected<GameCaptiveCreatureFeedResult> feed_captive_creature(
        uint64_t captive_entity_id, float tame_progress, uint64_t source_tick);

private:
    struct WildCreature {
        GameCreaturePresentationState state;
        uint64_t spawn_tick = 0;
        float wander_target_x = 0.0f;
        float wander_target_y = 0.0f;
        float wander_target_z = 0.0f;
        uint64_t next_wander_tick = 0;
        uint64_t next_player_attack_tick = 0;
    };

    [[nodiscard]] GameCreaturePresentationState make_wild_state(
        const GameEcosystemWildProxyRebalanceRequest& request,
        const GameEcosystemWildProxyPlan& plan, bool is_interactive) const;
    // The snapshot contains only interactive representatives from the same
    // chunk at the start of the ecosystem tick. It keeps reciprocal flee and
    // pursuit decisions deterministic regardless of map iteration order.
    [[nodiscard]] bool advance_wild_creature(
        WildCreature& creature,
        const std::vector<GameCreaturePresentationState>& nearby_creatures,
        const std::vector<GameWildCreaturePlayerTarget>& nearby_players,
        uint64_t source_tick);
    [[nodiscard]] const GameWildCreaturePlayerTarget* closest_player_target(
        const WildCreature& creature,
        const std::vector<GameWildCreaturePlayerTarget>& nearby_players,
        float detection_radius, float& out_distance_squared) const noexcept;
    void try_attack_player(WildCreature& creature,
                           const GameWildCreaturePlayerTarget& target,
                           float attack_damage, uint64_t attack_cooldown_ticks,
                           uint64_t source_tick);
    [[nodiscard]] bool choose_wild_wander_target(WildCreature& creature,
                                                  uint64_t source_tick) const;
    [[nodiscard]] bool move_wild_toward(WildCreature& creature,
                                        float target_x, float target_y,
                                        float target_z) const;
    [[nodiscard]] bool is_wild_cell_walkable(const ChunkKey& chunk,
                                              int32_t block_x, int32_t block_y,
                                              int32_t block_z) const;
    [[nodiscard]] bool should_emit_wild_update(uint64_t source_tick) const noexcept;
    [[nodiscard]] std::array<float, 3> choose_spawn_position(
        const ChunkKey& chunk, uint64_t stable_id) const;
    [[nodiscard]] static uint64_t stable_captive_runtime_id(
        const ChunkKey& chunk, uint16_t species_id, size_t slot) noexcept;
    [[nodiscard]] bool advance_captive_wander(const ChunkKey& chunk,
                                              CaptiveCreature& captive,
                                              uint64_t source_tick) const;
    [[nodiscard]] bool choose_captive_wander_target(const ChunkKey& chunk,
                                                     CaptiveCreature& captive,
                                                     uint64_t source_tick) const;
    [[nodiscard]] bool is_captive_cell_walkable(const ChunkKey& chunk,
                                                 int32_t block_x, int32_t block_y,
                                                 int32_t block_z) const;
    [[nodiscard]] static bool same_pen(const CaptiveCreature& left,
                                       const CaptiveCreature& right) noexcept;
    [[nodiscard]] static bool has_valid_pen_bounds(const CaptiveCreature& creature) noexcept;
    [[nodiscard]] static bool is_breedable(const CaptiveCreature& creature,
                                           int64_t current_tick) noexcept;
    [[nodiscard]] bool should_emit_captive_update(uint64_t source_tick) const noexcept;
    [[nodiscard]] static bool is_inside_pen(const GameCreaturePresentationState& creature,
                                            const GameCreaturePenBounds& bounds) noexcept;
    [[nodiscard]] GameCreaturePresentationState make_captive_state(
        const ChunkKey& chunk, const CaptiveCreature& creature) const;
    void emit(GameCreaturePresentationEventKind kind, uint64_t source_tick,
              const GameCreaturePresentationState& creature) const;

    GameEcosystemSystem* ecosystem_ = nullptr;
    ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    const CreatureSpeciesRegistry* species_catalog_ = nullptr;
    GameWildCreatureConfig config_;
    IGameCreaturePresentationSink* presentation_sink_ = nullptr;
    const IGameWildCreaturePlayerTargetProvider* player_target_provider_ = nullptr;
    IGameWildCreaturePlayerDamageSink* player_damage_sink_ = nullptr;
    std::optional<uint64_t> player_target_cache_tick_;
    std::vector<GameWildCreaturePlayerTarget> player_target_cache_;
    std::map<uint64_t, WildCreature> wild_creatures_;
    std::map<uint64_t, WildCreature> far_visual_creatures_;
    std::map<uint64_t, uint64_t> suppressed_proxy_until_tick_;
    std::map<uint64_t, GameCreaturePresentationState> known_captive_creatures_;
    std::map<std::string, uint64_t> player_damage_error_ticks_;
};

}  // namespace snt::game
