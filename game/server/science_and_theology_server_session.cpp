// Dedicated-server replication composition implementation.

#define SNT_LOG_CHANNEL "game.server_session"
#include "game/server/science_and_theology_server_session.h"

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
    return simulation_session_.register_content(services);
}

snt::core::Expected<void> ScienceAndTheologyServerSession::create_world(
    snt::engine::SimulationWorldSession& world) {
    if (auto result = simulation_session_.create_world(world); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyServerSession::create_world(simulation)");
        return error;
    }
    if (!config_.server_network.enabled) return {};

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

    peer_authenticator_ = std::make_unique<replication::ClosedGamePeerAuthenticator>();
    replication_handler_ = std::make_unique<replication::GameServerReplicationHandler>(
        *peer_authenticator_);
    transport_ = std::move(*transport);
    replication_service_ = std::make_unique<snt::network::ReplicationService>(
        *transport_, *replication_handler_);
    SNT_LOG_INFO("Dedicated server replication enabled (tcp=%u udp=%u max_peers=%u)",
                 transport_->tcp_port(), transport_->udp_port(), config_.server_network.max_peers);
    SNT_LOG_INFO("Gameplay player admission is closed until a game authenticator is configured");
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
    return simulation_session_.fixed_tick(context);
}

snt::core::Expected<void> ScienceAndTheologyServerSession::after_fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (auto result = simulation_session_.after_fixed_tick(context); !result) return result.error();
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
    if (replication_service_) replication_service_->shutdown();
    replication_service_.reset();
    transport_.reset();
    replication_handler_.reset();
    peer_authenticator_.reset();
    simulation_session_.shutdown();
}

}  // namespace snt::game
