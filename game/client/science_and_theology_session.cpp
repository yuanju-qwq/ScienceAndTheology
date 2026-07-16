// ScienceAndTheology graphical session implementation.

#define SNT_LOG_CHANNEL "game.client_session"
#include "science_and_theology_session.h"

#include "assets/asset_manager.h"
#include "core/events.h"
#include "core/log.h"
#include "render/render_components.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"
#include "ecs/world.h"
#include "engine/client_services.h"
#include "engine/simulation_services.h"
#include "game/player/player_replication.h"
#include "network/tcp_udp_transport.h"
#include "player/player_controller.h"
#include "player/player_physics_system.h"
#include "scene/scene.h"
#include "script/script_manager.h"
#include "ui/retained_mui.h"
#include "voxel/chunk_render_system.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace snt::game {
namespace {

constexpr uint64_t kMovementInputHeartbeatTicks = 4;

[[nodiscard]] bool same_movement_intent(
    const replication::GamePlayerMovementInput& left,
    const replication::GamePlayerMovementInput& right) noexcept {
    return left.forward_axis == right.forward_axis &&
           left.strafe_axis == right.strafe_axis &&
           left.flags == right.flags &&
           left.yaw_centidegrees == right.yaw_centidegrees &&
           left.pitch_centidegrees == right.pitch_centidegrees;
}

[[nodiscard]] float normalize_yaw(float yaw) noexcept {
    while (yaw < -180.0f) yaw += 360.0f;
    while (yaw >= 180.0f) yaw -= 360.0f;
    return yaw;
}

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

ScienceAndTheologyClientSession::ScienceAndTheologyClientSession(
    GameSessionConfig config,
    std::shared_ptr<localization::LocalizationService> localization,
    std::optional<replication::GameClientAuthentication> connection_authentication)
    : config_(std::move(config)), simulation_session_(config_),
      localization_(std::move(localization)),
      connection_authentication_(std::move(connection_authentication)) {
    if (connection_authentication_) {
        local_player_identity_ = connection_authentication_->local_identity;
    }
}

ScienceAndTheologyClientSession::~ScienceAndTheologyClientSession() { shutdown(); }

snt::core::Expected<void> ScienceAndTheologyClientSession::register_content(
    snt::engine::SimulationServices& services) {
    if (auto result = simulation_session_.register_content(services); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::register_content");
        return error;
    }
    services_ = &services;
    return {};
}

snt::core::Expected<void> ScienceAndTheologyClientSession::create_world(
    snt::engine::SimulationWorldSession& world_session) {
    if (auto result = simulation_session_.create_world(world_session); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_world");
        return error;
    }
    if (!config_.client_network.enabled) return {};
    if (!connection_authentication_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Client networking is enabled without injected player authentication"};
    }

    snt::network::TcpUdpConnectConfig connection_config;
    connection_config.host = config_.client_network.host;
    connection_config.tcp_port = config_.client_network.tcp_port;
    connection_config.udp_port = config_.client_network.udp_port;
    auto connection = replication::GameClientReplicationSession::connect_tcp_udp(
        std::move(connection_config), std::move(*connection_authentication_));
    if (!connection) {
        auto error = connection.error();
        error.with_context("ScienceAndTheologyClientSession::create_world(client replication)");
        return error;
    }
    connection_authentication_.reset();
    replication_session_ = std::move(*connection);
    remote_player_world_ = std::make_unique<replication::GameRemotePlayerWorld>(
        local_player_identity_ ? local_player_identity_->account_id : std::string{});
    SNT_LOG_INFO("Client replication connection requested (host=%s tcp=%u udp=%u)",
                 config_.client_network.host.c_str(), config_.client_network.tcp_port,
                 config_.client_network.udp_port);
    SNT_LOG_INFO("Client is awaiting the authoritative player AOI snapshot");
    return {};
}

snt::core::Expected<void> ScienceAndTheologyClientSession::create_client_world(
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
        error.with_context("ScienceAndTheologyClientSession::create_client_world(load_scene)");
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

    snt::player::PlayerControllerTuning tuning;
    tuning.move_speed = config_.camera.move_speed;
    tuning.look_speed = config_.camera.look_speed;
    auto& simulation = world_session.simulation();
    const bool has_authoritative_network_player = replication_session_ != nullptr;
    if (!has_authoritative_network_player) {
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
        player->set_tuning(tuning);
        auto player_physics = player->make_physics_system();
        if (auto result = simulation.register_main_system(player); !result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologyClientSession::create_client_world(register PlayerControllerSystem)");
            return error;
        }
        if (auto result = simulation.register_worker_system(std::move(player_physics)); !result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologyClientSession::create_client_world(register PlayerPhysicsSystem)");
            return error;
        }
        simulation.events().sink<snt::core::MouseLockChanged>()
            .connect<&snt::player::PlayerControllerSystem::on_mouse_lock_changed>(player.get());
    } else {
        presentation_world_ = &world;
        SNT_LOG_INFO("Client movement uses server-authoritative input and position updates");
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

    if (auto result = world_session.set_mouse_locked(true); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(mouse lock)");
        return error;
    }

    gameplay_ui_ = std::make_unique<GameplayUiController>(
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes());
    performance_ui_ = std::make_unique<PerformanceViewModel>();
    if (local_player_identity_) {
        SNT_LOG_INFO("Client local player identity is '%s' (%s)",
                     local_player_identity_->account_id.c_str(),
                     player_identity_provider_name(local_player_identity_->provider));
    }
    SNT_LOG_INFO("ScienceAndTheology client world and gameplay UI initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologyClientSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (replication_session_) {
        const snt::network::ReplicationTickContext replication_context{
            .tick_index = context.tick_index(),
            .delta_seconds = context.delta_seconds(),
        };
        if (auto result = replication_session_->poll_inbound(replication_context); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyClientSession::fixed_tick(replication inbound)");
            return error;
        }
        if (remote_player_world_ &&
            replication_session_->status().state ==
                replication::GameClientConnectionState::kDisconnected) {
            remote_player_world_->clear();
        } else if (remote_player_world_) {
            for (const replication::GameClientReplicationUpdate& update :
                 replication_session_->drain_replication_updates()) {
                const bool first_snapshot = remote_player_world_->active_snapshot_id() == 0;
                auto applied = std::visit(
                    [this](const auto& value) { return remote_player_world_->apply(value); }, update);
                if (!applied) {
                    auto error = applied.error();
                    error.with_context(
                        "ScienceAndTheologyClientSession::fixed_tick(remote player replication)");
                    return error;
                }
                if (first_snapshot &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative player snapshot %llu with %zu player value(s)",
                                 static_cast<unsigned long long>(
                                     remote_player_world_->active_snapshot_id()),
                                 remote_player_world_->player_count());
                }
            }
            apply_authoritative_local_player();
        }
        if (replication_session_->status().state ==
            replication::GameClientConnectionState::kAuthenticated) {
            const bool input_changed = !has_last_sent_movement_input_ ||
                !same_movement_intent(sampled_movement_input_, last_sent_movement_input_);
            const bool heartbeat_due = !has_last_sent_movement_input_ ||
                context.tick_index() - last_movement_send_tick_ >= kMovementInputHeartbeatTicks;
            if (input_changed || heartbeat_due) {
                auto input = sampled_movement_input_;
                input.client_sequence = next_movement_sequence_;
                if (auto result = replication_session_->enqueue_player_movement_input(input); !result) {
                    auto error = result.error();
                    error.with_context("ScienceAndTheologyClientSession::fixed_tick(player movement input)");
                    return error;
                }
                last_sent_movement_input_ = input;
                last_sent_movement_input_.client_sequence = 0;
                has_last_sent_movement_input_ = true;
                last_movement_send_tick_ = context.tick_index();
                ++next_movement_sequence_;
            }
        }
    }
    return simulation_session_.fixed_tick(context);
}

snt::core::Expected<void> ScienceAndTheologyClientSession::after_fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (auto result = simulation_session_.after_fixed_tick(context); !result) return result.error();
    if (!replication_session_) return {};

    const snt::network::ReplicationTickContext replication_context{
        .tick_index = context.tick_index(),
        .delta_seconds = context.delta_seconds(),
    };
    if (auto result = replication_session_->emit_outbound(replication_context); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::after_fixed_tick(replication outbound)");
        return error;
    }
    return {};
}

void ScienceAndTheologyClientSession::frame(snt::engine::ClientFrameContext& context) {
    handle_gameplay_input(context);
    sample_network_movement_input(context);

    if (config_.scripts.enabled && services_) {
        if (context.input().key_pressed[SDL_SCANCODE_F5]) {
            if (auto result = context.services().scripts().reload_all(); !result) {
                SNT_LOG_ERROR("Gameplay script reload failed: %s", result.error().format().c_str());
            } else {
                SNT_LOG_INFO("Gameplay script reload completed");
            }
        }
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

void ScienceAndTheologyClientSession::build_ui(snt::engine::ClientUiContext& context) {
    if (gameplay_ui_ && localization_) {
        auto root = build_gameplay_ui_root(
            *gameplay_ui_, {context.viewport_width(), context.viewport_height()}, *localization_);
        context.submit(std::move(root), snt::ui::UiLayer::Screen);
    }
    if (performance_ui_ && performance_ui_->visible() && localization_) {
        auto panel = build_performance_panel_view(*performance_ui_, *localization_);
        context.submit(std::move(panel), snt::ui::UiLayer::Debug);
    }
    if (context.mouse_locked()) draw_crosshair(context);
}

void ScienceAndTheologyClientSession::shutdown() noexcept {
    presentation_world_ = nullptr;
    remote_player_world_.reset();
    if (replication_session_) replication_session_->shutdown();
    replication_session_.reset();
    connection_authentication_.reset();
    gameplay_ui_.reset();
    performance_ui_.reset();
    localization_.reset();
    simulation_session_.shutdown();
    services_ = nullptr;
}

void ScienceAndTheologyClientSession::sample_network_movement_input(
    snt::engine::ClientFrameContext& context) {
    if (!replication_session_ || presentation_world_ == nullptr) return;
    const entt::entity camera_entity =
        presentation_world_->find_entity_by_guid(snt::ecs::EntityGuid{config_.scene.active_camera_guid});
    if (camera_entity == entt::null ||
        !presentation_world_->registry().all_of<snt::render::Transform>(camera_entity)) {
        return;
    }

    auto& transform = presentation_world_->registry().get<snt::render::Transform>(camera_entity);
    const auto& input = context.input();
    if (context.mouse_locked()) {
        transform.rotation[1] = normalize_yaw(
            transform.rotation[1] + input.mouse_dx * config_.camera.look_speed);
        transform.rotation[0] = std::clamp(
            transform.rotation[0] - input.mouse_dy * config_.camera.look_speed, -89.0f, 89.0f);
    }

    sampled_movement_input_ = {};
    if (!context.mouse_locked()) return;
    if (input.key_held[SDL_SCANCODE_W]) ++sampled_movement_input_.forward_axis;
    if (input.key_held[SDL_SCANCODE_S]) --sampled_movement_input_.forward_axis;
    if (input.key_held[SDL_SCANCODE_D]) ++sampled_movement_input_.strafe_axis;
    if (input.key_held[SDL_SCANCODE_A]) --sampled_movement_input_.strafe_axis;
    if (input.key_held[SDL_SCANCODE_LSHIFT]) {
        sampled_movement_input_.flags |= replication::kGamePlayerMovementFlagSprint;
    }
    if (input.key_pressed[SDL_SCANCODE_SPACE]) {
        sampled_movement_input_.flags |= replication::kGamePlayerMovementFlagJump;
    }
    const int yaw_centidegrees = std::clamp(
        static_cast<int>(std::lround(normalize_yaw(transform.rotation[1]) * 100.0f)),
        -18000, 17999);
    sampled_movement_input_.yaw_centidegrees = static_cast<int16_t>(yaw_centidegrees);
    sampled_movement_input_.pitch_centidegrees = static_cast<int16_t>(std::lround(
        std::clamp(transform.rotation[0], -89.0f, 89.0f) * 100.0f));
}

void ScienceAndTheologyClientSession::apply_authoritative_local_player() {
    if (presentation_world_ == nullptr || !remote_player_world_) return;
    const auto player = remote_player_world_->authoritative_local_player();
    if (!player.has_value()) return;
    const entt::entity camera_entity =
        presentation_world_->find_entity_by_guid(snt::ecs::EntityGuid{config_.scene.active_camera_guid});
    if (camera_entity == entt::null ||
        !presentation_world_->registry().all_of<snt::render::Transform>(camera_entity)) {
        return;
    }
    auto& transform = presentation_world_->registry().get<snt::render::Transform>(camera_entity);
    transform.position[0] = static_cast<float>(player->player.position.position.x);
    transform.position[1] = static_cast<float>(player->player.position.position.y) + 1.62f;
    transform.position[2] = static_cast<float>(player->player.position.position.z);
}

void ScienceAndTheologyClientSession::handle_gameplay_input(snt::engine::ClientFrameContext& context) {
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

void ScienceAndTheologyClientSession::draw_crosshair(snt::engine::ClientUiContext& context) const {
    const float center_x = context.viewport_width() * 0.5f;
    const float center_y = context.viewport_height() * 0.5f;
    snt::ui::Arc2DCommandBuffer commands;
    append_cross_arm(commands, center_x, center_y, 1.0f, 1.0f, {0, 0, 0, 160});
    append_cross_arm(commands, center_x, center_y, 0.0f, 0.0f, {255, 255, 255, 220});
    context.submit(std::move(commands), snt::ui::UiLayer::Hud);
}

}  // namespace snt::game
