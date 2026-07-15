// Dedicated-server replication composition implementation.

#define SNT_LOG_CHANNEL "game.server_session"
#include "game/server/science_and_theology_server_session.h"

#include "game/network/game_account_peer_authenticator.h"
#include "game/server/game_server_command_sink.h"
#include "game/server/game_server_player_lifecycle.h"
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
    if (auto result = simulation_session_.create_world(world); !result) {
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
    command_sink_ = std::make_unique<replication::GameServerCommandSink>(simulation_session_.quests());
    player_lifecycle_ = std::make_unique<replication::GameServerPlayerLifecycle>(
        simulation_session_.quests(),
        services_->paths().resolve_user(config_.persistence.universe_save_dir),
        config_.persistence.player_progress_autosave_interval_ticks);
    replication_handler_ = std::make_unique<replication::GameServerReplicationHandler>(
        *peer_authenticator_, command_sink_.get(), player_lifecycle_.get());
    transport_ = std::move(*transport);
    replication_service_ = std::make_unique<snt::network::ReplicationService>(
        *transport_, *replication_handler_);
    SNT_LOG_INFO("Dedicated server replication enabled (tcp=%u udp=%u max_peers=%u)",
                 transport_->tcp_port(), transport_->udp_port(), config_.server_network.max_peers);
    SNT_LOG_INFO("Gameplay local-name admission is enabled; duplicate local names take over one account session");
    SNT_LOG_INFO("Authenticated player quest state persists below '%s'",
                 player_lifecycle_->universe_save_dir().c_str());
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
    peer_authenticator_.reset();
    command_sink_.reset();
    player_lifecycle_.reset();
    services_ = nullptr;
    simulation_session_.shutdown();
}

}  // namespace snt::game
