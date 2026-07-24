// Native client projection for replicated ground-loot presentation.
//
// Ownership: GameChunkSidecar remains the durable authority and
// GameRemoteGroundLootWorld owns the replicated value cache. This adapter
// creates only transient ECS entities, keeping renderer state and pickup
// semantics independently replaceable.

#pragma once

#include "ecs/entt_config.h"
#include "game/network/game_ground_loot_replication.h"
#include "render/render_components.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::assets {
class AssetManager;
}

namespace snt::ecs {
class World;
}

namespace snt::game {

// Renderer-side metadata keeps a stable id and full content stack available
// for future inspect/tooltips without turning ECS entities into world state.
struct GameGroundLootPresentationComponent {
    uint64_t loot_id = 0;
    ResourceContentStack resource;
    uint64_t spawned_tick = 0;
};

struct GameGroundLootPresentationVisual {
    snt::render::MeshRef mesh;
    float model_scale = 0.28f;
    float vertical_offset_blocks = 0.14f;
};

// Item-specific meshes can be registered as authored assets arrive. The
// default marker keeps all replicated loot visible in the meantime.
class GameGroundLootPresentationVisualCatalog final {
public:
    void set_default_visual(GameGroundLootPresentationVisual visual);
    void set_item_visual(std::string item_id, GameGroundLootPresentationVisual visual);
    [[nodiscard]] std::optional<GameGroundLootPresentationVisual> resolve(
        const ResourceContentStack& resource) const;

private:
    std::optional<GameGroundLootPresentationVisual> default_visual_;
    std::map<std::string, GameGroundLootPresentationVisual, std::less<>> item_visuals_;
};

[[nodiscard]] snt::core::Expected<GameGroundLootPresentationVisualCatalog>
make_default_ground_loot_presentation_visual_catalog(snt::assets::AssetManager& assets);

class GameGroundLootPresentationWorld final {
public:
    GameGroundLootPresentationWorld(
        snt::ecs::World& world, GameGroundLootPresentationVisualCatalog visuals) noexcept;

    GameGroundLootPresentationWorld(const GameGroundLootPresentationWorld&) = delete;
    GameGroundLootPresentationWorld& operator=(const GameGroundLootPresentationWorld&) = delete;

    // Each replication update contains the complete visible set for this
    // adapter. Omitted records are removed only from client presentation.
    void reconcile(
        std::span<const replication::GameGroundLootPresentationState> loot,
        uint64_t source_tick);
    void clear() noexcept;

    [[nodiscard]] size_t loot_count() const noexcept { return entities_.size(); }
    [[nodiscard]] std::optional<entt::entity> entity_for(uint64_t loot_id) const;

private:
    void upsert(const replication::GameGroundLootPresentationState& loot,
                uint64_t source_tick);
    void remove(uint64_t loot_id, uint64_t source_tick) noexcept;

    snt::ecs::World* world_ = nullptr;
    GameGroundLootPresentationVisualCatalog visuals_;
    std::map<uint64_t, entt::entity> entities_;
    std::unordered_set<std::string> missing_visuals_logged_;
};

}  // namespace snt::game
