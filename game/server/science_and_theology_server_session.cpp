// Dedicated-server replication composition implementation.

#define SNT_LOG_CHANNEL "game.server_session"
#include "game/server/science_and_theology_server_session.h"

#include "game/network/game_account_peer_authenticator.h"
#include "game/server/game_server_command_sink.h"
#include "game/server/game_server_player_death.h"
#include "game/server/game_server_player_interaction.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_movement.h"
#include "game/server/game_server_quest_book_replication.h"
#include "game/server/game_server_quest_events.h"
#include "game/server/game_server_player_replication.h"
#include "game/server/game_server_player_state.h"
#include "network/replication.h"
#include "network/tcp_udp_transport.h"

#include "core/error.h"
#include "core/log.h"
#include "engine/simulation_services.h"

#include <string>
#include <utility>

namespace snt::game {

ScienceAndTheologyServerSession::ScienceAndTheologyServerSession(GameSessionConfig config)
    : config_(std::move(config)), simulation_session_(config_) {}

ScienceAndTheologyServerSession::~ScienceAndTheologyServerSession() { shutdown(); }

snt::core::Expected<void> ScienceAndTheologyServerSession::register_content(
    snt::engine::SimulationServices& services) {
    services_ = &services;
    if (auto result = simulation_session_.register_content(services); !result) {
        services_ = nullptr;
        return result.error();
    }
    return {};
}

snt::core::Expected<void> ScienceAndTheologyServerSession::create_world(
    snt::engine::SimulationWorldSession& world) {
    if (config_.server_network.enabled) {
        quest_events_ = std::make_unique<replication::GameServerQuestEventService>(
            simulation_session_.quests());
        simulation_session_.set_machine_tick_event_sink(quest_events_.get());
        simulation_session_.set_quest_reward_sink(quest_events_.get());
    }
    if (auto result = simulation_session_.create_world(world); !result) {
        simulation_session_.set_machine_tick_event_sink(nullptr);
        simulation_session_.set_quest_reward_sink(nullptr);
        quest_events_.reset();
        auto error = result.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(simulation)");
        return error;
    }
    if (!config_.server_network.enabled) return {};
    if (services_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Dedicated server services are unavailable"};
    }
    if (config_.persistence.universe_save_dir.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Dedicated server universe_save_dir must not be empty"};
    }

    snt::network::TcpUdpListenConfig transport_config;
    transport_config.bind_address = config_.server_network.bind_address;
    transport_config.tcp_port = config_.server_network.tcp_port;
    transport_config.udp_port = config_.server_network.udp_port;
    transport_config.replication.max_peers = config_.server_network.max_peers;
    auto transport = snt::network::TcpUdpReplicationTransport::listen(std::move(transport_config));
    if (!transport) {
        auto error = transport.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(TCP+UDP listen)");
        return error;
    }

    // Local names intentionally provide LAN-style account identity. A future
    // Steamworks package injects a verifier here; Steam login stays rejected
    // until then instead of trusting a client-supplied SteamID.
    peer_authenticator_ = std::make_unique<replication::GameAccountPeerAuthenticator>();
    auto player_state = replication::GameServerPlayerState::create(
        world.world(),
        {
            .spawn = {
                .dimension_id = config_.persistence.world_dimension_id,
                .position = {
                    .x = config_.server_player.spawn_block_x,
                    .y = config_.server_player.spawn_block_y,
                    .z = config_.server_player.spawn_block_z,
                },
            },
            .inventory_slots = config_.server_player.inventory_slots,
            .inventory_max_stack_size = config_.server_player.inventory_max_stack_size,
            .interaction_reach_blocks = config_.server_player.interaction_reach_blocks,
        });
    if (!player_state) {
        auto error = player_state.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(authoritative player state)");
        return error;
    }
    player_state_ = std::move(*player_state);
    auto player_movement = replication::GameServerPlayerMovement::create(
        *player_state_, world.chunks(),
        {
            .walk_speed_blocks_per_second =
                config_.server_player.movement_walk_speed_blocks_per_second,
            .sprint_multiplier = config_.server_player.movement_sprint_multiplier,
            .jump_speed_blocks_per_second =
                config_.server_player.movement_jump_speed_blocks_per_second,
            .gravity_blocks_per_second_squared =
                config_.server_player.movement_gravity_blocks_per_second_squared,
            .terminal_velocity_blocks_per_second =
                config_.server_player.movement_terminal_velocity_blocks_per_second,
            .body_width_blocks = config_.server_player.movement_body_width_blocks,
            .body_height_blocks = config_.server_player.movement_body_height_blocks,
            .input_timeout_ticks = config_.server_player.movement_input_timeout_ticks,
            .missing_chunks_are_solid = config_.server_player.movement_missing_chunks_are_solid,
        });
    if (!player_movement) {
        auto error = player_movement.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(authoritative player movement)");
        return error;
    }
    player_movement_ = std::move(*player_movement);
    auto quest_book_replication = replication::GameServerQuestBookReplication::create(
        simulation_session_.quests());
    if (!quest_book_replication) {
        auto error = quest_book_replication.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(task-book replication)");
        return error;
    }
    quest_book_replication_ = std::move(*quest_book_replication);
    auto player_replication = replication::GameServerPlayerReplication::create(
        *player_state_, world.world(), world.chunks(), simulation_session_.world_sidecars(),
        {
            .horizontal_aoi_radius_blocks =
                config_.server_replication.player_horizontal_aoi_radius_blocks,
            .vertical_aoi_radius_blocks =
                config_.server_replication.player_vertical_aoi_radius_blocks,
            .max_visible_players = config_.server_replication.max_visible_players,
            .chunk_horizontal_aoi_radius_blocks =
                config_.server_replication.chunk_horizontal_aoi_radius_blocks,
            .chunk_vertical_aoi_radius_blocks =
                config_.server_replication.chunk_vertical_aoi_radius_blocks,
            .max_visible_chunks = config_.server_replication.max_visible_chunks,
        },
        {quest_book_replication_.get()});
    if (!player_replication) {
        auto error = player_replication.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player AOI)");
        return error;
    }
    player_replication_ = std::move(*player_replication);
    player_lifecycle_ = std::make_unique<replication::GameServerPlayerLifecycle>(
        simulation_session_.quests(), *player_state_,
        services_->paths().resolve_user(config_.persistence.universe_save_dir),
        config_.persistence.player_progress_autosave_interval_ticks);
    quest_events_->bind_player_state(*player_state_, player_lifecycle_.get());
    auto player_beds = replication::GameServerPlayerBedService::create(
        *player_state_, world.chunks(), simulation_session_.world_sidecars(),
        player_lifecycle_.get());
    if (!player_beds) {
        auto error = player_beds.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player beds)");
        return error;
    }
    player_beds_ = std::move(*player_beds);
    auto player_graves = replication::GameServerPlayerGraveStore::create(
        world.chunks(), simulation_session_.world_sidecars(),
        {
            .grave_material_id = config_.server_player.grave_material_id,
            .air_material_id = 0,
            .vertical_search_blocks = config_.server_player.grave_vertical_search_blocks,
        });
    if (!player_graves) {
        auto error = player_graves.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player graves)");
        return error;
    }
    player_graves_ = std::move(*player_graves);
    auto player_respawn = replication::GameServerPlayerRespawnResolver::create(
        world.chunks(), *player_beds_,
        {
            .world_spawn = {
                .dimension_id = config_.persistence.world_dimension_id,
                .position = {
                    .x = config_.server_player.spawn_block_x,
                    .y = config_.server_player.spawn_block_y,
                    .z = config_.server_player.spawn_block_z,
                },
            },
            .world_spawn_search_radius_blocks =
                config_.server_player.respawn_safe_search_radius_blocks,
        });
    if (!player_respawn) {
        auto error = player_respawn.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player respawn)");
        return error;
    }
    player_respawn_ = std::move(*player_respawn);
    auto player_death = replication::GameServerPlayerDeathService::create(
        *player_state_, *player_graves_, *player_respawn_, player_lifecycle_.get(),
        player_movement_.get());
    if (!player_death) {
        auto error = player_death.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player death)");
        return error;
    }
    player_death_ = std::move(*player_death);
    auto player_interactions = replication::GameServerPlayerInteractionService::create(
        world.world(), world.chunks(), simulation_session_.world_sidecars(), *player_state_,
        *player_beds_, simulation_session_.content(), simulation_session_.machine_interactions(),
        player_lifecycle_.get(),
        {quest_events_.get(), player_replication_.get()},
        {
            .air_material_id = 0,
            .reserved_grave_material_id = config_.server_player.grave_material_id,
        });
    if (!player_interactions) {
        auto error = player_interactions.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(player interactions)");
        return error;
    }
    player_interactions_ = std::move(*player_interactions);
    command_sink_ = std::make_unique<replication::GameServerCommandSink>(
        simulation_session_.quests(), player_movement_.get(), player_interactions_.get());
    const replication::GameReplicationBudget replication_budget{
        .max_reliable_bytes_per_tick = config_.server_replication.max_reliable_bytes_per_tick,
        .max_chunk_snapshots_per_tick = config_.server_replication.max_chunk_snapshots_per_tick,
        .max_entity_snapshots_per_tick = config_.server_replication.max_entity_snapshots_per_tick,
        .max_value_snapshots_per_tick = config_.server_replication.max_value_snapshots_per_tick,
        .max_block_deltas_per_tick = config_.server_replication.max_block_deltas_per_tick,
    };
    replication_handler_ = std::make_unique<replication::GameServerReplicationHandler>(
        *peer_authenticator_, command_sink_.get(), player_lifecycle_.get(),
        player_replication_.get(), player_replication_.get(), replication_budget);
    transport_ = std::move(*transport);
    replication_service_ = std::make_unique<snt::network::ReplicationService>(
        *transport_, *replication_handler_);
    SNT_LOG_INFO("Dedicated server replication enabled (tcp=%u udp=%u max_peers=%u)",
                 transport_->tcp_port(), transport_->udp_port(), config_.server_network.max_peers);
    SNT_LOG_INFO("Gameplay local-name admission is enabled; duplicate local names take over one account session");
    SNT_LOG_INFO("Authenticated player quest state persists below '%s'",
                 player_lifecycle_->universe_save_dir().c_str());
    SNT_LOG_INFO("Player AOI replication enabled (horizontal=%u vertical=%u visible=%u entity_budget=%u)",
                 config_.server_replication.player_horizontal_aoi_radius_blocks,
                 config_.server_replication.player_vertical_aoi_radius_blocks,
                 config_.server_replication.max_visible_players,
                 config_.server_replication.max_entity_snapshots_per_tick);
    SNT_LOG_INFO("Task-book value replication enabled (value_budget=%u)",
                 config_.server_replication.max_value_snapshots_per_tick);
    SNT_LOG_INFO("Authoritative player movement enabled (walk=%.2f sprint=%.2f input_timeout=%llu ticks)",
                 config_.server_player.movement_walk_speed_blocks_per_second,
                 config_.server_player.movement_sprint_multiplier,
                 static_cast<unsigned long long>(
                     config_.server_player.movement_input_timeout_ticks));
    SNT_LOG_INFO("Player bed/grave services enabled (grave_material=%u vertical_search=%u respawn_search=%u)",
                 config_.server_player.grave_material_id,
                 config_.server_player.grave_vertical_search_blocks,
                 config_.server_player.respawn_safe_search_radius_blocks);
    SNT_LOG_INFO("Host block interactions enabled: client raycast and machine prerequisite hints are trusted; host commits shared world/inventory state");
    SNT_LOG_INFO("Committed gameplay events drive host quest progress; item rewards await explicit task-book claims");
    SNT_LOG_INFO("Steam login remains unavailable until a server ticket verifier is installed");
    return {};
}

snt::core::Expected<void> ScienceAndTheologyServerSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (replication_service_) {
        const snt::network::ReplicationTickContext replication_context{
            .tick_index = context.tick_index(),
            .delta_seconds = context.delta_seconds(),
        };
        if (auto result = replication_service_->poll_inbound(replication_context); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyServerSession::fixed_tick(replication inbound)");
            return error;
        }
    }
    if (command_sink_) {
        if (auto result = command_sink_->apply_pending_commands(context.tick_index()); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyServerSession::fixed_tick(game commands)");
            return error;
        }
    }
    if (player_movement_) {
        if (auto result = player_movement_->tick({
                .tick_index = context.tick_index(),
                .delta_seconds = context.delta_seconds(),
            });
            !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyServerSession::fixed_tick(player movement)");
            return error;
        }
    }
    return simulation_session_.fixed_tick(context);
}

snt::core::Expected<void> ScienceAndTheologyServerSession::after_fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (auto result = simulation_session_.after_fixed_tick(context); !result) return result.error();
    if (player_lifecycle_) {
        if (auto result = player_lifecycle_->flush_due(context.tick_index()); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyServerSession::after_fixed_tick(player autosave)");
            return error;
        }
    }
    if (!replication_service_) return {};

    const snt::network::ReplicationTickContext replication_context{
        .tick_index = context.tick_index(),
        .delta_seconds = context.delta_seconds(),
    };
    if (auto result = replication_service_->emit_outbound(replication_context); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyServerSession::after_fixed_tick(replication outbound)");
        return error;
    }
    return {};
}

void ScienceAndTheologyServerSession::shutdown() noexcept {
    if (player_lifecycle_) player_lifecycle_->shutdown();
    if (replication_service_) replication_service_->shutdown();
    replication_service_.reset();
    transport_.reset();
    replication_handler_.reset();
    command_sink_.reset();
    player_replication_.reset();
    quest_book_replication_.reset();
    player_interactions_.reset();
    simulation_session_.set_machine_tick_event_sink(nullptr);
    simulation_session_.set_quest_reward_sink(nullptr);
    if (quest_events_) quest_events_->unbind_player_state();
    quest_events_.reset();
    player_death_.reset();
    player_respawn_.reset();
    player_graves_.reset();
    player_beds_.reset();
    peer_authenticator_.reset();
    player_movement_.reset();
    player_lifecycle_.reset();
    if (player_state_) player_state_->shutdown();
    player_state_.reset();
    services_ = nullptr;
    simulation_session_.shutdown();
}

}  // namespace snt::game
