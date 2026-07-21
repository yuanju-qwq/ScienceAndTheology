// Native client projection for ecosystem creature presentation implementation.

#define SNT_LOG_CHANNEL "game.creature_presentation"
#include "game/client/creature_presentation_world.h"

#include "assets/asset_manager.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/game_components.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] bool valid_role(CreatureRole role) noexcept {
    return static_cast<size_t>(role) < static_cast<size_t>(CreatureRole::COUNT);
}

[[nodiscard]] float resolved_scale(float value) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

[[nodiscard]] snt::core::Expected<snt::render::MeshHandle> resolve_creature_mesh(
    snt::assets::AssetManager& assets, const char* path) {
    auto mesh = assets.resolve_mesh(path);
    if (!mesh) {
        auto error = mesh.error();
        error.with_context(std::string("make_default_creature_presentation_visual_catalog('") +
                           path + "')");
        return error;
    }
    return *mesh;
}

}  // namespace

void GameCreaturePresentationVisualCatalog::set_species_visual(
    uint16_t species_id, GameCreaturePresentationVisual visual) {
    if (species_id == 0) return;
    species_visuals_.insert_or_assign(species_id, std::move(visual));
}

void GameCreaturePresentationVisualCatalog::set_role_visual(
    CreatureRole role, GameCreaturePresentationVisual visual) {
    if (!valid_role(role)) return;
    role_visuals_[static_cast<size_t>(role)] = std::move(visual);
}

std::optional<GameCreaturePresentationVisual>
GameCreaturePresentationVisualCatalog::resolve(uint16_t species_id, CreatureRole role) const {
    if (const auto species = species_visuals_.find(species_id);
        species != species_visuals_.end()) {
        return species->second;
    }
    if (!valid_role(role)) return std::nullopt;
    return role_visuals_[static_cast<size_t>(role)];
}

snt::core::Expected<GameCreaturePresentationVisualCatalog>
make_default_creature_presentation_visual_catalog(
    snt::assets::AssetManager& assets, const CreatureSpeciesRegistry& species_catalog) {
    auto herbivore_full = resolve_creature_mesh(assets, "assets/creatures/herbivore_full.obj");
    if (!herbivore_full) return herbivore_full.error();
    auto herbivore_lod = resolve_creature_mesh(assets, "assets/creatures/herbivore_lod.obj");
    if (!herbivore_lod) return herbivore_lod.error();
    auto predator_full = resolve_creature_mesh(assets, "assets/creatures/predator_full.obj");
    if (!predator_full) return predator_full.error();
    auto predator_lod = resolve_creature_mesh(assets, "assets/creatures/predator_lod.obj");
    if (!predator_lod) return predator_lod.error();

    GameCreaturePresentationVisualCatalog result;
    result.set_role_visual(CreatureRole::HERBIVORE, {
        .mesh = {.handle = *herbivore_full},
        .lod = {
            .simplified_handle = *herbivore_lod,
            .simplified_detail_distance = 14.0f,
            .cull_distance = 80.0f,
        },
    });
    result.set_role_visual(CreatureRole::PREDATOR, {
        .mesh = {.handle = *predator_full},
        .lod = {
            .simplified_handle = *predator_lod,
            .simplified_detail_distance = 14.0f,
            .cull_distance = 80.0f,
        },
    });

    for (const uint16_t species_id : species_catalog.all_species_ids()) {
        const CreatureSpeciesDef* const species = species_catalog.get_species(species_id);
        if (species == nullptr) continue;
        auto visual = result.resolve(0, species->role);
        if (!visual.has_value()) continue;
        visual->model_scale = resolved_scale(species->model_scale);
        result.set_species_visual(species_id, std::move(*visual));
    }
    return result;
}

GameCreaturePresentationWorld::GameCreaturePresentationWorld(
    snt::ecs::World& world, GameCreaturePresentationVisualCatalog visuals) noexcept
    : world_(&world), visuals_(std::move(visuals)) {}

void GameCreaturePresentationWorld::on_creature_presentation_event(
    const GameCreaturePresentationEvent& event) {
    if (event.kind == GameCreaturePresentationEventKind::kDespawned) {
        remove(event.creature.entity_id, event.source_tick);
        return;
    }
    upsert(event.creature, event.source_tick);
}

void GameCreaturePresentationWorld::reconcile(
    std::span<const GameCreaturePresentationState> creatures, uint64_t source_tick) {
    std::unordered_set<uint64_t> desired_ids;
    desired_ids.reserve(creatures.size());
    for (const GameCreaturePresentationState& creature : creatures) {
        if (creature.entity_id == 0) continue;
        desired_ids.insert(creature.entity_id);
        upsert(creature, source_tick);
    }

    std::vector<uint64_t> removals;
    removals.reserve(entities_.size());
    for (const auto& [creature_id, entity] : entities_) {
        static_cast<void>(entity);
        if (!desired_ids.contains(creature_id)) removals.push_back(creature_id);
    }
    for (const uint64_t creature_id : removals) remove(creature_id, source_tick);
}

void GameCreaturePresentationWorld::clear() noexcept {
    if (world_ == nullptr) {
        entities_.clear();
        return;
    }
    for (const auto& [creature_id, entity] : entities_) {
        static_cast<void>(creature_id);
        if (world_->registry().valid(entity)) world_->destroy_entity(entity);
    }
    entities_.clear();
}

std::optional<entt::entity> GameCreaturePresentationWorld::entity_for(
    uint64_t creature_id) const {
    const auto found = entities_.find(creature_id);
    if (found == entities_.end()) return std::nullopt;
    return found->second;
}

void GameCreaturePresentationWorld::upsert(
    const GameCreaturePresentationState& creature, uint64_t source_tick) {
    static_cast<void>(source_tick);
    if (world_ == nullptr || creature.entity_id == 0) return;
    const auto visual = visuals_.resolve(creature.species_id, creature.role);
    if (!visual.has_value() || !visual->mesh.handle.valid()) {
        if (missing_visuals_logged_.insert(creature.species_id).second) {
            SNT_LOG_WARN("No native visual is configured for creature species %u",
                         static_cast<unsigned>(creature.species_id));
        }
        remove(creature.entity_id, source_tick);
        return;
    }

    entt::entity entity = entt::null;
    if (const auto found = entities_.find(creature.entity_id); found != entities_.end() &&
        world_->registry().valid(found->second)) {
        entity = found->second;
    } else {
        if (found != entities_.end()) entities_.erase(found);
        entity = world_->create_entity();
        entities_.emplace(creature.entity_id, entity);
    }

    snt::render::Transform transform;
    transform.position[0] = creature.position_x;
    transform.position[1] = creature.position_y;
    transform.position[2] = creature.position_z;
    const float scale = resolved_scale(visual->model_scale);
    transform.scale[0] = scale;
    transform.scale[1] = scale;
    transform.scale[2] = scale;
    world_->registry().emplace_or_replace<snt::render::Transform>(entity, transform);
    world_->registry().emplace_or_replace<snt::render::MeshRef>(entity, visual->mesh);
    world_->registry().emplace_or_replace<snt::render::MeshLod>(entity, visual->lod);
    GameCreaturePresentationComponent metadata{
        .creature_id = creature.entity_id,
        .species_id = creature.species_id,
        .is_interactive = creature.is_interactive,
        .is_captive = creature.is_captive,
        .is_tamed = creature.is_tamed,
    };
    world_->registry().emplace_or_replace<GameCreaturePresentationComponent>(
        entity, metadata);
    if (!world_->registry().all_of<CreatureMarker>(entity)) {
        world_->registry().emplace<CreatureMarker>(entity);
    }
}

void GameCreaturePresentationWorld::remove(uint64_t creature_id, uint64_t source_tick) noexcept {
    static_cast<void>(source_tick);
    if (creature_id == 0) return;
    const auto found = entities_.find(creature_id);
    if (found == entities_.end()) return;
    if (world_ != nullptr && world_->registry().valid(found->second)) {
        world_->destroy_entity(found->second);
    }
    entities_.erase(found);
}

}  // namespace snt::game
