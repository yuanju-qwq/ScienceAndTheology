// ScienceAndTheology game content session.

#define SNT_LOG_CHANNEL "game_session"
#include "science_and_theology_session.h"

#include "demo_world_bootstrap.h"

#include "assets/asset_manager.h"
#include "core/events.h"
#include "core/log.h"
#include "render/render_components.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"
#include "ecs/world.h"
#include "engine/client_services.h"
#include "engine/simulation_services.h"
#include "machine_tick_system.h"
#include "player/player_controller.h"
#include "player/player_physics_system.h"
#include "scene/scene.h"
#include "script/script_manager.h"
#include "ui/retained_mui.h"
#include "voxel/chunk_render_system.h"

#include <SDL3/SDL_scancode.h>

#include <filesystem>
#include <memory>
#include <utility>

namespace snt::game {
namespace {

void append_cross_arm(snt::ui::Arc2DCommandBuffer& commands, float center_x, float center_y,
                      float offset_x, float offset_y, snt::ui::Color color) {
    constexpr float kArmLength = 8.0f;
    constexpr float kGap = 4.0f;
    constexpr float kThickness = 2.0f;
    commands.rect({.pos = {center_x - kGap - kArmLength + offset_x,
                           center_y - kThickness * 0.5f + offset_y},
                   .size = {kArmLength, kThickness}}, color);
    commands.rect({.pos = {center_x + kGap + offset_x,
                           center_y - kThickness * 0.5f + offset_y},
                   .size = {kArmLength, kThickness}}, color);
    commands.rect({.pos = {center_x - kThickness * 0.5f + offset_x,
                           center_y - kGap - kArmLength + offset_y},
                   .size = {kThickness, kArmLength}}, color);
    commands.rect({.pos = {center_x - kThickness * 0.5f + offset_x,
                           center_y + kGap + offset_y},
                   .size = {kThickness, kArmLength}}, color);
}

}  // namespace

ScienceAndTheologySession::ScienceAndTheologySession(GameSessionConfig config)
    : config_(std::move(config)) {}

ScienceAndTheologySession::~ScienceAndTheologySession() { shutdown(); }

snt::core::Expected<void> ScienceAndTheologySession::register_content(
        snt::engine::SimulationServices& services) {
    services_ = &services;
    if (!config_.scripts.enabled) return {};

    auto& scripts = services.scripts();
    if (auto result = scripts.set_content_host(content_registry_); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::register_content(content host)");
        return error;
    }
    if (auto result = scripts.init(); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::register_content(ScriptManager)");
        return error;
    }
    scripts_started_ = true;

    const std::filesystem::path root(services.paths().resolve_game(config_.scripts.root));
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code) || error_code) {
        SNT_LOG_INFO("Gameplay script root not present; no modules loaded: %s", root.string().c_str());
        return {};
    }

    const auto result = config_.scripts.watch_for_changes
        ? scripts.watch_directory(root)
        : scripts.load_directory(root.string());
    if (!result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::register_content(gameplay scripts)");
        return error;
    }
    return {};
}

snt::core::Expected<void> ScienceAndTheologySession::create_world(
        snt::engine::SimulationWorldSession& world_session) {
    if (!services_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game session services are unavailable"};
    }

    if (scripts_started_) {
        auto machine_system = std::make_shared<MachineTickSystem>(content_registry_);
        if (auto result = world_session.register_worker_system(std::move(machine_system)); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologySession::create_world(register MachineTickSystem)");
            return error;
        }
    }

    if (auto result = bootstrap_demo_world(config_.demo, world_session.chunks(), chunk_sidecars_); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::create_world(bootstrap_demo_world)");
        return error;
    }

    SNT_LOG_INFO("ScienceAndTheology simulation world initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologySession::create_client_world(
        snt::engine::ClientWorldSession& world_session) {
    if (!services_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game session services are unavailable"};
    }

    auto& world = world_session.world();
    auto scene_result = snt::scene::load_scene(
        world, world_session.assets(), services_->paths().resolve_game(config_.scene.path));
    if (!scene_result) {
        auto error = scene_result.error();
        error.with_context("ScienceAndTheologySession::create_client_world(load_scene)");
        return error;
    }

    const snt::ecs::EntityGuid camera_guid{config_.scene.active_camera_guid};
    const entt::entity camera_entity = world.find_entity_by_guid(camera_guid);
    if (camera_entity == entt::null ||
        !world.registry().all_of<snt::render::Transform, snt::render::Camera>(camera_entity)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Configured active camera is absent from the game scene"};
    }

    // These cubes are only scene-generator verification content. The client
    // session owns their removal now that terrain is its primary presentation.
    for (const uint64_t guid : {2ull, 3ull}) {
        const entt::entity entity = world.find_entity_by_guid(snt::ecs::EntityGuid{guid});
        if (entity != entt::null) world.destroy_entity(entity);
    }

    auto& camera = world.registry().get<snt::render::Camera>(camera_entity);
    const auto& runtime_config = services_->config();
    camera.aspect = static_cast<float>(runtime_config.window.width) /
                    static_cast<float>(runtime_config.window.height);
    camera.fov = config_.camera.fov;
    camera.near_plane = config_.camera.near_plane;
    camera.far_plane = config_.camera.far_plane;
    if (auto result = world_session.set_active_camera(camera_guid); !result) {
        return result.error();
    }

    // Terrain generation belongs to the simulation session. Presentation only
    // observes its resulting generic chunk keys and schedules initial meshes.
    for (const auto& key : world_session.chunks().all_chunk_keys()) {
        world_session.chunk_render_system().mark_dirty(key);
    }

    auto player = std::make_shared<snt::player::PlayerControllerSystem>();
    player->set_input(&world_session.input());
    player->set_chunk_registry(&world_session.chunks());
    player->set_chunk_render_system(&world_session.chunk_render_system());
    player->set_camera_entity(camera_entity);
    player->set_dimension_id("overworld");
    player->set_spawn_feet_position({config_.camera.initial_feet_position[0],
                                     config_.camera.initial_feet_position[1],
                                     config_.camera.initial_feet_position[2]});
    player->set_initial_look(-90.0f, -25.0f);
    snt::player::PlayerControllerTuning tuning;
    tuning.move_speed = config_.camera.move_speed;
    tuning.look_speed = config_.camera.look_speed;
    player->set_tuning(tuning);
    auto player_physics = player->make_physics_system();
    auto& simulation = world_session.simulation();
    if (auto result = simulation.register_main_system(player); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::create_client_world(register PlayerControllerSystem)");
        return error;
    }
    if (auto result = simulation.register_worker_system(std::move(player_physics)); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::create_client_world(register PlayerPhysicsSystem)");
        return error;
    }

    // Match PlayerPhysicsSystem::sync_camera_transform before the first
    // fixed tick so the initial rendered frame uses the configured spawn.
    auto& camera_transform = world.registry().get<snt::render::Transform>(camera_entity);
    camera_transform.position[0] = config_.camera.initial_feet_position[0];
    camera_transform.position[1] = config_.camera.initial_feet_position[1] + tuning.eye_height;
    camera_transform.position[2] = config_.camera.initial_feet_position[2];
    camera_transform.rotation[0] = -25.0f;
    camera_transform.rotation[1] = -90.0f;
    camera_transform.rotation[2] = 0.0f;

    simulation.events().sink<snt::core::MouseLockChanged>()
        .connect<&snt::player::PlayerControllerSystem::on_mouse_lock_changed>(player.get());
    if (auto result = world_session.set_mouse_locked(true); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySession::create_client_world(mouse lock)");
        return error;
    }

    gameplay_ui_ = std::make_unique<GameplayUiController>(
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes());
    performance_ui_ = std::make_unique<PerformanceViewModel>();
    SNT_LOG_INFO("ScienceAndTheology client world and gameplay UI initialized");
    return {};
}

void ScienceAndTheologySession::fixed_tick(snt::engine::FixedTickContext&) {
    // Scheduler-owned gameplay systems run immediately after this hook at the
    // deterministic SimulationRuntime fixed-tick boundary.
}

void ScienceAndTheologySession::frame(snt::engine::ClientFrameContext& context) {
    handle_gameplay_input(context);

    if (scripts_started_) {
        if (context.input().key_pressed[SDL_SCANCODE_F5]) {
            if (auto result = context.services().scripts().reload_all(); !result) {
                SNT_LOG_ERROR("Gameplay script reload failed: %s", result.error().format().c_str());
            } else {
                SNT_LOG_INFO("Gameplay script reload completed");
            }
        }
        context.services().scripts().update(context.delta_seconds());
    }

    if (performance_ui_) {
        const auto& stats = context.stats();
        performance_ui_->publish({
            .fps = stats.fps,
            .frame_ms = stats.frame_ms,
            .tps = stats.tps,
            .mspt = stats.mspt,
            .job_workers = stats.job_workers,
        });
    }
}

void ScienceAndTheologySession::build_ui(snt::engine::ClientUiContext& context) {
    if (gameplay_ui_) {
        auto root = build_gameplay_ui_root(
            *gameplay_ui_, {context.viewport_width(), context.viewport_height()});
        context.submit(*root);
    }
    if (performance_ui_ && performance_ui_->visible()) {
        auto panel = build_performance_panel_view(*performance_ui_);
        context.submit(*panel);
    }
    if (context.mouse_locked()) draw_crosshair(context);
}

void ScienceAndTheologySession::shutdown() noexcept {
    if (scripts_started_ && services_) {
        services_->scripts().shutdown();
        scripts_started_ = false;
    }
    gameplay_ui_.reset();
    performance_ui_.reset();
    services_ = nullptr;
}

void ScienceAndTheologySession::handle_gameplay_input(snt::engine::ClientFrameContext& context) {
    if (!gameplay_ui_) return;
    const auto& input = context.input();

    if (input.key_pressed[SDL_SCANCODE_E]) {
        gameplay_ui_->toggle_inventory();
        SNT_LOG_INFO("Inventory UI %s", gameplay_ui_->inventory_open() ? "opened" : "closed");
    }
    if (input.key_pressed[SDL_SCANCODE_C]) {
        gameplay_ui_->toggle_crafting();
        SNT_LOG_INFO("Crafting UI %s", gameplay_ui_->crafting_open() ? "opened" : "closed");
    }
    if (input.key_pressed[SDL_SCANCODE_RETURN] && gameplay_ui_->crafting_open()) {
        for (const auto& recipe : gameplay_ui_->crafting().recipes()) {
            if (!gameplay_ui_->crafting().can_craft(recipe)) continue;
            const auto result = gameplay_ui_->crafting().craft(recipe.id);
            if (result.ok) {
                SNT_LOG_INFO("Crafted %s x%d", result.output.item_key.c_str(), result.output.count);
            } else {
                SNT_LOG_WARN("Craft failed: %s", result.reason.c_str());
            }
            break;
        }
    }
    if (input.key_pressed[SDL_SCANCODE_F3] && performance_ui_) {
        performance_ui_->toggle_visible();
        SNT_LOG_INFO("Performance UI %s", performance_ui_->visible() ? "opened" : "closed");
    }

    bool gameplay_ui_open = gameplay_ui_->inventory_open() || gameplay_ui_->crafting_open();
    if (input.esc_pressed && gameplay_ui_open) {
        gameplay_ui_->close();
        gameplay_ui_open = false;
        SNT_LOG_INFO("Gameplay UI closed");
    }
    if ((input.esc_pressed || gameplay_ui_open) && context.mouse_locked()) {
        context.set_mouse_locked(false);
    } else if (input.wants_mouse_lock && !context.mouse_locked() && !gameplay_ui_open) {
        context.set_mouse_locked(true);
    }
}

void ScienceAndTheologySession::draw_crosshair(snt::engine::ClientUiContext& context) const {
    const float center_x = context.viewport_width() * 0.5f;
    const float center_y = context.viewport_height() * 0.5f;
    snt::ui::Arc2DCommandBuffer commands;
    append_cross_arm(commands, center_x, center_y, 1.0f, 1.0f, {0, 0, 0, 160});
    append_cross_arm(commands, center_x, center_y, 0.0f, 0.0f, {255, 255, 255, 220});
    context.submit(commands);
}

}  // namespace snt::game
