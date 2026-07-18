// Game-facing LAN server discovery implementation.

#include "game/network/game_lan_discovery.h"

#include "core/error.h"
#include "game/network/game_replication_protocol.h"
#include "network/lan_discovery.h"

#include <utility>

namespace snt::game::replication {

GameLanDiscoveryClient::GameLanDiscoveryClient(
    std::unique_ptr<snt::network::LanDiscoveryClient> client)
    : client_(std::move(client)) {}

GameLanDiscoveryClient::~GameLanDiscoveryClient() { shutdown(); }

snt::core::Expected<std::unique_ptr<GameLanDiscoveryClient>> GameLanDiscoveryClient::create(
    GameLanDiscoveryClientConfig config) {
    auto client = snt::network::LanDiscoveryClient::create({
        .target_address = std::move(config.target_address),
        .port = config.port,
    });
    if (!client) {
        auto error = client.error();
        error.with_context("GameLanDiscoveryClient::create");
        return error;
    }
    return std::unique_ptr<GameLanDiscoveryClient>(new GameLanDiscoveryClient(std::move(*client)));
}

snt::core::Expected<void> GameLanDiscoveryClient::query() {
    if (!client_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game LAN discovery client is stopped"};
    }
    auto result = client_->query();
    if (!result) {
        auto error = result.error();
        error.with_context("GameLanDiscoveryClient::query");
        return error;
    }
    return {};
}

snt::core::Expected<std::vector<GameLanDiscoveredServer>> GameLanDiscoveryClient::poll() {
    if (!client_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game LAN discovery client is stopped"};
    }
    auto replies = client_->poll();
    if (!replies) {
        auto error = replies.error();
        error.with_context("GameLanDiscoveryClient::poll");
        return error;
    }

    std::vector<GameLanDiscoveredServer> servers;
    servers.reserve(replies->size());
    for (auto& reply : *replies) {
        if (reply.advertisement.application_protocol_version !=
            kCurrentGameReplicationProtocolVersion) {
            continue;
        }
        servers.push_back({
            .host = std::move(reply.host),
            .server_name = std::move(reply.advertisement.server_name),
            .tcp_port = reply.advertisement.tcp_port,
            .udp_port = reply.advertisement.udp_port,
            .current_players = reply.advertisement.current_players,
            .max_players = reply.advertisement.max_players,
            .password_required = reply.advertisement.password_required,
        });
    }
    return servers;
}

uint16_t GameLanDiscoveryClient::local_port() const noexcept {
    return client_ ? client_->local_port() : 0;
}

void GameLanDiscoveryClient::shutdown() noexcept {
    if (client_) client_->shutdown();
    client_.reset();
}

}  // namespace snt::game::replication
