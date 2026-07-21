// Near-field wild creature projection and interaction system.
//
// Ownership: GameEcosystemSystem owns persistent aggregate wild population.
// This module owns only temporary interactive wild representatives and writes
// durable CaptiveCreature values through GameChunkSidecarRegistry after a
// confirmed capture. It contains no Godot, renderer, network, or ECS handles.

#pragma once

#include "core/expected.h"
#include "game/simulation/ecosystem_system.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace snt::game {

struct GameCreaturePresentationState {
    uint64_t entity_id = 0;
    ChunkKey chunk;
    uint16_t species_id = 0;
    CreatureRole role = CreatureRole::HERBIVORE;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float health = 1.0f;
    // A far visual representative is presentation-only. Only an
    // interactive representative can be passed to the authority operations
    // below, even though both use the same stable population proxy id.
    bool is_interactive = false;
    bool is_captive = false;
    bool is_tamed = false;
};

enum class GameCreaturePresentationEventKind : uint8_t {
    kSpawned,
    kDespawned,
    kDamaged,
    kKilled,
    kCaptured,
    kTamingProgressed,
    kTamed,
};

struct GameCreaturePresentationEvent {
    GameCreaturePresentationEventKind kind = GameCreaturePresentationEventKind::kSpawned;
    uint64_t source_tick = 0;
    GameCreaturePresentationState creature;
};

class IGameCreaturePresentationSink {
public:
    virtual ~IGameCreaturePresentationSink() = default;
    virtual void on_creature_presentation_event(
        const GameCreaturePresentationEvent& event) = 0;
};

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

struct GameCaptiveCreatureFeedResult {
    bool fed = false;
    bool became_tamed = false;
    uint64_t captive_entity_id = 0;
    uint16_t species_id = 0;
    float tame_progress = 0.0f;
    ChunkKey chunk;
};

struct GameWildCreatureConfig {
    uint32_t max_captive_creatures_per_chunk = 64;
    uint64_t killed_proxy_suppression_ticks = 120;
    uint64_t captured_proxy_suppression_ticks = 300;
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

    void request_wild_proxy_rebalance(
        const GameEcosystemWildProxyRebalanceRequest& request) override;
    void request_far_visual_rebalance(
        const GameEcosystemFarVisualRebalanceRequest& request) override;
    void tick_captive_creatures(const GameEcosystemCaptiveTickRequest& request) override;

    [[nodiscard]] std::optional<GameCreaturePresentationState> find_wild_creature(
        uint64_t wild_entity_id) const;
    [[nodiscard]] size_t wild_creature_count() const noexcept { return wild_creatures_.size(); }
    [[nodiscard]] size_t far_visual_creature_count() const noexcept {
        return far_visual_creatures_.size();
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
    };

    [[nodiscard]] GameCreaturePresentationState make_wild_state(
        const GameEcosystemWildProxyRebalanceRequest& request,
        const GameEcosystemWildProxyPlan& plan, bool is_interactive) const;
    [[nodiscard]] std::array<float, 3> choose_spawn_position(
        const ChunkKey& chunk, uint64_t stable_id) const;
    [[nodiscard]] static uint64_t stable_captive_runtime_id(
        const ChunkKey& chunk, uint16_t species_id, size_t slot) noexcept;
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
    std::map<uint64_t, WildCreature> wild_creatures_;
    std::map<uint64_t, WildCreature> far_visual_creatures_;
    std::map<uint64_t, uint64_t> suppressed_proxy_until_tick_;
    std::set<uint64_t> known_captive_runtime_ids_;
};

}  // namespace snt::game
