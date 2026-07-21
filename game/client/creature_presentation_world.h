// Native client projection for ecosystem creature presentation.
//
// Ownership: GameWildCreatureSystem emits value-only events; this module owns
// the corresponding transient client ECS entities. It never owns population,
// combat, capture, or taming state and can therefore be replaced by a
// multiplayer replication adapter without changing simulation code.

#pragma once

#include "ecs/entt_config.h"
#include "game/simulation/wild_creature_system.h"
#include "render/render_components.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>

namespace snt::assets {
class AssetManager;
}

namespace snt::ecs {
class World;
}

namespace snt::game {

// Client-only metadata retained beside the generic renderer components. It
// gives future native picking/UI code a stable creature target without making
// renderer entities part of the headless wildlife authority.
struct GameCreaturePresentationComponent {
    uint64_t creature_id = 0;
    uint16_t species_id = 0;
    bool is_interactive = false;
    bool is_captive = false;
    bool is_tamed = false;
};

struct GameCreaturePresentationVisual {
    snt::render::MeshRef mesh;
    snt::render::MeshLod lod;
    float model_scale = 1.0f;
};

// Species overrides preserve room for dedicated models. Role fallbacks let
// the initial native content reuse a small full-detail and LOD mesh set while
// keeping the gameplay catalog independent from renderer handles.
class GameCreaturePresentationVisualCatalog final {
public:
    void set_species_visual(uint16_t species_id, GameCreaturePresentationVisual visual);
    void set_role_visual(CreatureRole role, GameCreaturePresentationVisual visual);
    [[nodiscard]] std::optional<GameCreaturePresentationVisual> resolve(
        uint16_t species_id, CreatureRole role) const;

private:
    std::map<uint16_t, GameCreaturePresentationVisual> species_visuals_;
    std::array<std::optional<GameCreaturePresentationVisual>,
               static_cast<size_t>(CreatureRole::COUNT)> role_visuals_;
};

// Resolves the built-in native OBJ assets into a catalog. The caller may add
// species overrides after this returns; the fallback models deliberately
// remain role-based until authored species meshes are introduced.
[[nodiscard]] snt::core::Expected<GameCreaturePresentationVisualCatalog>
make_default_creature_presentation_visual_catalog(
    snt::assets::AssetManager& assets, const CreatureSpeciesRegistry& species_catalog);

class GameCreaturePresentationWorld final : public IGameCreaturePresentationSink {
public:
    GameCreaturePresentationWorld(snt::ecs::World& world,
                                  GameCreaturePresentationVisualCatalog visuals) noexcept;

    GameCreaturePresentationWorld(const GameCreaturePresentationWorld&) = delete;
    GameCreaturePresentationWorld& operator=(const GameCreaturePresentationWorld&) = delete;

    void on_creature_presentation_event(
        const GameCreaturePresentationEvent& event) override;

    // Network replication can provide a complete current visible set instead
    // of individual simulation events. Reconciliation uses the same upsert
    // path and removes only presentation entities owned by this adapter.
    void reconcile(std::span<const GameCreaturePresentationState> creatures,
                   uint64_t source_tick);
    void clear() noexcept;

    [[nodiscard]] size_t creature_count() const noexcept { return entities_.size(); }
    [[nodiscard]] std::optional<entt::entity> entity_for(uint64_t creature_id) const;

private:
    void upsert(const GameCreaturePresentationState& creature, uint64_t source_tick);
    void remove(uint64_t creature_id, uint64_t source_tick) noexcept;

    snt::ecs::World* world_ = nullptr;
    GameCreaturePresentationVisualCatalog visuals_;
    std::map<uint64_t, entt::entity> entities_;
    std::unordered_set<uint16_t> missing_visuals_logged_;
};

}  // namespace snt::game
