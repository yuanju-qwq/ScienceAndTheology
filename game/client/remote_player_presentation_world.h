// Native client projection for interpolated remote-player avatars.
//
// Ownership: GameRemotePlayerWorld owns ordered replicated values and
// GameRemotePlayerInterpolator owns timing. This adapter owns only transient
// renderer entities, so avatar meshes never become transport, ECS authority,
// or persistent player state.

#pragma once

#include "ecs/entt_config.h"
#include "game/client/player_motion_presentation.h"
#include "game/player/player_replication.h"
#include "render/render_components.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>

namespace snt::assets {
class AssetManager;
}

namespace snt::ecs {
class World;
}

namespace snt::game {

// Renderer-side metadata gives future native labels, picking, and appearance
// work a stable target without exposing inventory instances or server handles.
struct GameRemotePlayerPresentationComponent {
    snt::ecs::EntityGuid player_guid;
    std::string display_name;
    std::array<std::string, kGamePlayerEquipmentSlotCount> equipment_item_ids;
    uint64_t source_tick = 0;
    bool grounded = false;
};

struct GameRemotePlayerPresentationVisual {
    snt::render::MeshRef mesh;
    snt::render::MeshLod lod;
    float model_scale = 1.0f;
};

[[nodiscard]] snt::core::Expected<GameRemotePlayerPresentationVisual>
make_default_remote_player_presentation_visual(snt::assets::AssetManager& assets);

class GameRemotePlayerPresentationWorld final {
public:
    GameRemotePlayerPresentationWorld(
        snt::ecs::World& world, GameRemotePlayerPresentationVisual visual) noexcept;

    GameRemotePlayerPresentationWorld(const GameRemotePlayerPresentationWorld&) = delete;
    GameRemotePlayerPresentationWorld& operator=(const GameRemotePlayerPresentationWorld&) = delete;

    // The replicated collection is complete for the local player's current
    // AOI. Each frame samples the independently maintained interpolation cache
    // before updating only entities owned by this adapter.
    void reconcile(std::span<const replication::GameRemotePlayerState> players,
                   const GameRemotePlayerInterpolator& interpolator,
                   uint64_t client_presentation_tick);
    void clear() noexcept;

    [[nodiscard]] size_t player_count() const noexcept { return entities_.size(); }
    [[nodiscard]] std::optional<entt::entity> entity_for(snt::ecs::EntityGuid player_guid) const;

private:
    void upsert(const replication::GameRemotePlayerState& player,
                const GameClientPlayerPresentationState& presentation);
    void remove(uint64_t player_guid) noexcept;

    snt::ecs::World* world_ = nullptr;
    GameRemotePlayerPresentationVisual visual_;
    std::map<uint64_t, entt::entity> entities_;
    std::unordered_set<uint64_t> invalid_presentations_logged_;
    bool missing_visual_logged_ = false;
};

}  // namespace snt::game
