// Native client projection for replicated ground-loot presentation implementation.

#define SNT_LOG_CHANNEL "game.ground_loot_presentation"
#include "game/client/ground_loot_presentation_world.h"

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
    return std::isfinite(value) && value > 0.0f ? value : 0.28f;
}

[[nodiscard]] float resolved_vertical_offset(float value) noexcept {
    return std::isfinite(value) ? value : 0.14f;
}

[[nodiscard]] snt::core::Expected<snt::render::MeshHandle> resolve_ground_loot_mesh(
    snt::assets::AssetManager& assets, const char* path) {
    auto mesh = assets.resolve_mesh(path);
    if (!mesh) {
        auto error = mesh.error();
        error.with_context(std::string("make_default_ground_loot_presentation_visual_catalog('") +
                           path + "')");
        return error;
    }
    return *mesh;
}

}  // namespace

void GameGroundLootPresentationVisualCatalog::set_default_visual(
    GameGroundLootPresentationVisual visual) {
    default_visual_ = std::move(visual);
}

void GameGroundLootPresentationVisualCatalog::set_item_visual(
    std::string item_id, GameGroundLootPresentationVisual visual) {
    if (item_id.empty()) return;
    item_visuals_.insert_or_assign(std::move(item_id), std::move(visual));
}

std::optional<GameGroundLootPresentationVisual>
GameGroundLootPresentationVisualCatalog::resolve(const ResourceContentStack& resource) const {
    if (resource.is_item()) {
        if (const auto found = item_visuals_.find(resource.key.id); found != item_visuals_.end()) {
            return found->second;
        }
    }
    return default_visual_;
}

snt::core::Expected<GameGroundLootPresentationVisualCatalog>
make_default_ground_loot_presentation_visual_catalog(snt::assets::AssetManager& assets) {
    auto marker = resolve_ground_loot_mesh(assets, "assets/loot/ground_loot_marker.obj");
    if (!marker) return marker.error();

    GameGroundLootPresentationVisualCatalog result;
    result.set_default_visual({
        .mesh = {.handle = *marker},
        .model_scale = 0.28f,
        .vertical_offset_blocks = 0.14f,
    });
    return result;
}

GameGroundLootPresentationWorld::GameGroundLootPresentationWorld(
    snt::ecs::World& world, GameGroundLootPresentationVisualCatalog visuals) noexcept
    : world_(&world), visuals_(std::move(visuals)) {}

void GameGroundLootPresentationWorld::reconcile(
    std::span<const replication::GameGroundLootPresentationState> loot,
    uint64_t source_tick) {
    std::unordered_set<uint64_t> desired_ids;
    desired_ids.reserve(loot.size());
    for (const replication::GameGroundLootPresentationState& state : loot) {
        if (state.loot_id == 0) continue;
        desired_ids.insert(state.loot_id);
        upsert(state, source_tick);
    }

    std::vector<uint64_t> removals;
    removals.reserve(entities_.size());
    for (const auto& [loot_id, entity] : entities_) {
        static_cast<void>(entity);
        if (!desired_ids.contains(loot_id)) removals.push_back(loot_id);
    }
    for (const uint64_t loot_id : removals) remove(loot_id, source_tick);
}

void GameGroundLootPresentationWorld::clear() noexcept {
    if (world_ == nullptr) {
        entities_.clear();
        return;
    }
    for (const auto& [loot_id, entity] : entities_) {
        static_cast<void>(loot_id);
        if (world_->registry().valid(entity)) world_->destroy_entity(entity);
    }
    entities_.clear();
}

std::optional<entt::entity> GameGroundLootPresentationWorld::entity_for(uint64_t loot_id) const {
    const auto found = entities_.find(loot_id);
    if (found == entities_.end()) return std::nullopt;
    return found->second;
}

void GameGroundLootPresentationWorld::upsert(
    const replication::GameGroundLootPresentationState& loot, uint64_t source_tick) {
    static_cast<void>(source_tick);
    if (world_ == nullptr || loot.loot_id == 0 || !loot.resource.is_item() ||
        !std::isfinite(loot.position_x) || !std::isfinite(loot.position_y) ||
        !std::isfinite(loot.position_z)) {
        return;
    }
    const auto visual = visuals_.resolve(loot.resource);
    if (!visual.has_value() || !visual->mesh.handle.valid()) {
        if (missing_visuals_logged_.insert(loot.resource.key.id).second) {
            SNT_LOG_WARN("No native visual is configured for ground loot item '%s'",
                         loot.resource.key.id.c_str());
        }
        remove(loot.loot_id, source_tick);
        return;
    }

    entt::entity entity = entt::null;
    if (const auto found = entities_.find(loot.loot_id); found != entities_.end() &&
        world_->registry().valid(found->second)) {
        entity = found->second;
    } else {
        if (found != entities_.end()) entities_.erase(found);
        entity = world_->create_entity();
        entities_.emplace(loot.loot_id, entity);
    }

    const float scale = resolved_scale(visual->model_scale);
    snt::render::Transform transform;
    transform.position[0] = loot.position_x;
    transform.position[1] = loot.position_y + resolved_vertical_offset(visual->vertical_offset_blocks);
    transform.position[2] = loot.position_z;
    transform.rotation[1] = static_cast<float>(loot.loot_id % 360u);
    transform.scale[0] = scale;
    transform.scale[1] = scale;
    transform.scale[2] = scale;
    world_->registry().emplace_or_replace<snt::render::Transform>(entity, transform);
    world_->registry().emplace_or_replace<snt::render::MeshRef>(entity, visual->mesh);
    world_->registry().emplace_or_replace<GameGroundLootPresentationComponent>(
        entity,
        GameGroundLootPresentationComponent{
            .loot_id = loot.loot_id,
            .resource = loot.resource,
            .spawned_tick = loot.spawned_tick,
        });
}

void GameGroundLootPresentationWorld::remove(uint64_t loot_id, uint64_t source_tick) noexcept {
    static_cast<void>(source_tick);
    if (loot_id == 0) return;
    const auto found = entities_.find(loot_id);
    if (found == entities_.end()) return;
    if (world_ != nullptr && world_->registry().valid(found->second)) {
        world_->destroy_entity(found->second);
    }
    entities_.erase(found);
}

}  // namespace snt::game
