// Game-owned account authenticator implementation.

#define SNT_LOG_CHANNEL "game.replication"
#include "game/network/game_account_peer_authenticator.h"

#include "core/error.h"

#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

}  // namespace

GameAccountPeerAuthenticator::GameAccountPeerAuthenticator(
    ISteamSessionTicketVerifier* steam_ticket_verifier, bool allow_local_name_accounts)
    : steam_ticket_verifier_(steam_ticket_verifier),
      allow_local_name_accounts_(allow_local_name_accounts) {}

snt::core::Expected<GameAuthenticatedPeer> GameAccountPeerAuthenticator::authenticate(
    snt::network::PeerId peer, const GameLoginRequest& request,
    const snt::network::ReplicationTickContext& context) {
    switch (request.identity_provider) {
        case PlayerIdentityProvider::kLocalName: {
            if (!allow_local_name_accounts_) {
                return protocol_error("Local-name player accounts are disabled by this server");
            }
            if (!request.credential.empty()) {
                return protocol_error("Local-name player accounts must not send credentials");
            }
            auto identity = make_local_name_player_identity(request.display_name);
            if (!identity) {
                auto error = identity.error();
                error.with_context("GameAccountPeerAuthenticator::authenticate(local name)");
                return error;
            }
            return GameAuthenticatedPeer{.identity = std::move(*identity)};
        }
        case PlayerIdentityProvider::kSteam: {
            if (steam_ticket_verifier_ == nullptr) {
                return snt::core::Error{
                    snt::core::ErrorCode::kNotImplemented,
                    "Steam login was requested but this server has no Steam ticket verifier"};
            }
            auto verified = steam_ticket_verifier_->verify_ticket(peer, request.credential, context);
            if (!verified) {
                auto error = verified.error();
                error.with_context("GameAccountPeerAuthenticator::authenticate(Steam ticket)");
                return error;
            }
            auto identity = make_steam_player_identity(verified->steam_id,
                                                       std::move(verified->display_name));
            if (!identity) {
                auto error = identity.error();
                error.with_context("GameAccountPeerAuthenticator::authenticate(Steam identity)");
                return error;
            }
            return GameAuthenticatedPeer{.identity = std::move(*identity)};
        }
    }
    return protocol_error("Game login identity provider is invalid");
}

void GameAccountPeerAuthenticator::on_peer_disconnected(const GameAuthenticatedPeer&,
                                                         std::string_view) noexcept {}

}  // namespace snt::game::replication
