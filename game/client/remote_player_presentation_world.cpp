// Native client projection for interpolated remote-player avatars implementation.

#define SNT_LOG_CHANNEL "game.remote_player_presentation"
#include "game/client/remote_player_presentation_world.h"

#include "assets/asset_manager.h"
#include "core/log.h"
#include "ecs/world.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] float resolved_scale(float value) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

[[nodiscard]] bool valid_presentation(const GameClientPlayerPresentationState& presentation) noexcept {
    return !presentation.dimension_id.empty() && std::isfinite(presentation.feet_x) &&
           std::isfinite(presentation.feet_y) && std::isfinite(presentation.feet_z);
}

[[nodiscard]] snt::core::Expected<snt::render::MeshHandle> resolve_remote_player_mesh(
    snt::assets::AssetManager& assets, const char* path) {
    auto mesh = assets.resolve_mesh(path);
    if (!mesh) {
        auto error = mesh.error();
        error.with_context(std::string("make_default_remote_player_presentation_visual('") +
                           path + "')");
        return error;
    }
    return *mesh;
}

}  // namespace

snt::core::Expected<GameRemotePlayerPresentationVisual>
make_default_remote_player_presentation_visual(snt::assets::AssetManager& assets) {
    auto full = resolve_remote_player_mesh(assets, "assets/players/remote_avatar_full.obj");
    if (!full) return full.error();
    auto lod = resolve_remote_player_mesh(assets, "assets/players/remote_avatar_lod.obj");
    if (!lod) return lod.error();

    return GameRemotePlayerPresentationVisual{
        .mesh = {.handle = *full},
        .lod = {
            .simplified_handle = *lod,
            .simplified_detail_distance = 18.0f,
            .cull_distance = 96.0f,
        },
        .model_scale = 1.0f,
    };
}

GameRemotePlayerPresentationWorld::GameRemotePlayerPresentationWorld(
    snt::ecs::World& world, GameRemotePlayerPresentationVisual visual) noexcept
    : world_(&world), visual_(std::move(visual)) {}

void GameRemotePlayerPresentationWorld::reconcile(
    std::span<const replication::GameRemotePlayerState> players,
    const GameRemotePlayerInterpolator& interpolator,
    uint64_t client_presentation_tick) {
    std::unordered_set<uint64_t> desired_guids;
    desired_guids.reserve(players.size());
    for (const replication::GameRemotePlayerState& player : players) {
        if (!player.entity_guid.valid()) continue;
        desired_guids.insert(player.entity_guid.value);

        const std::optional<GameClientPlayerPresentationState> presentation =
            interpolator.sample(player.entity_guid, client_presentation_tick);
        if (!presentation.has_value()) {
            remove(player.entity_guid.value);
            continue;
        }
        upsert(player, *presentation);
    }

    std::vector<uint64_t> removals;
    removals.reserve(entities_.size());
    for (const auto& [player_guid, entity] : entities_) {
        static_cast<void>(entity);
        if (!desired_guids.contains(player_guid)) removals.push_back(player_guid);
    }
    for (const uint64_t player_guid : removals) remove(player_guid);
}

void GameRemotePlayerPresentationWorld::clear() noexcept {
    if (world_ != nullptr) {
        for (const auto& [player_guid, entity] : entities_) {
            static_cast<void>(player_guid);
            if (world_->registry().valid(entity)) world_->destroy_entity(entity);
        }
    }
    entities_.clear();
    invalid_presentations_logged_.clear();
    missing_visual_logged_ = false;
}

std::optional<entt::entity> GameRemotePlayerPresentationWorld::entity_for(
    snt::ecs::EntityGuid player_guid) const {
    if (!player_guid.valid()) return std::nullopt;
    const auto found = entities_.find(player_guid.value);
    if (found == entities_.end()) return std::nullopt;
    return found->second;
}

void GameRemotePlayerPresentationWorld::upsert(
    const replication::GameRemotePlayerState& player,
    const GameClientPlayerPresentationState& presentation) {
    if (world_ == nullptr || !player.entity_guid.valid()) return;
    if (!valid_presentation(presentation)) {
        if (invalid_presentations_logged_.insert(player.entity_guid.value).second) {
            SNT_LOG_WARN("Ignoring invalid interpolated remote-player presentation (guid=%llu)",
                         static_cast<unsigned long long>(player.entity_guid.value));
        }
        remove(player.entity_guid.value);
        return;
    }
    if (!visual_.mesh.handle.valid()) {
        if (!missing_visual_logged_) {
            SNT_LOG_WARN("Remote player avatar presentation has no native mesh");
            missing_visual_logged_ = true;
        }
        remove(player.entity_guid.value);
        return;
    }

    entt::entity entity = entt::null;
    if (const auto found = entities_.find(player.entity_guid.value); found != entities_.end() &&
        world_->registry().valid(found->second)) {
        entity = found->second;
    } else {
        if (found != entities_.end()) entities_.erase(found);
        entity = world_->create_entity();
        entities_.emplace(player.entity_guid.value, entity);
    }

    const float scale = resolved_scale(visual_.model_scale);
    snt::render::Transform transform;
    transform.position[0] = presentation.feet_x;
    transform.position[1] = presentation.feet_y;
    transform.position[2] = presentation.feet_z;
    // A body follows yaw only. Pitch remains camera/head presentation data for
    // a later rigged avatar layer and must not tilt the whole standing mesh.
    transform.rotation[1] = static_cast<float>(presentation.yaw_centidegrees) / 100.0f;
    transform.scale[0] = scale;
    transform.scale[1] = scale;
    transform.scale[2] = scale;
    world_->registry().emplace_or_replace<snt::render::Transform>(entity, transform);
    world_->registry().emplace_or_replace<snt::render::MeshRef>(entity, visual_.mesh);
    world_->registry().emplace_or_replace<snt::render::MeshLod>(entity, visual_.lod);
    world_->registry().emplace_or_replace<GameRemotePlayerPresentationComponent>(
        entity,
        GameRemotePlayerPresentationComponent{
            .player_guid = player.entity_guid,
            .display_name = player.player.identity.display_name,
            .equipment_item_ids = player.player.equipment_item_ids,
            .source_tick = presentation.source_tick,
            .grounded = presentation.grounded,
        });
}

void GameRemotePlayerPresentationWorld::remove(uint64_t player_guid) noexcept {
    const auto found = entities_.find(player_guid);
    if (found == entities_.end()) return;
    if (world_ != nullptr && world_->registry().valid(found->second)) {
        world_->destroy_entity(found->second);
    }
    entities_.erase(found);
}

}  // namespace snt::game
