// Game-owned account authenticator for local-name and verified Steam players.
//
// Local names intentionally behave as collision-prone LAN accounts: identical
// validated names map to the same local-name account id. The replication
// handler gives the newer login that account session and disconnects the old
// peer. Steam ids are created only from a server-side ticket verifier, never
// from client-supplied identity strings.

#pragma once

#include "game/network/game_replication_services.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace snt::game::replication {

struct VerifiedSteamAccount {
    uint64_t steam_id = 0;
    std::string display_name;
};

// A Steamworks package adapter implements this contract and remains outside
// the headless game network target. The verifier receives opaque ticket bytes,
// validates them server-side, and returns the authoritative SteamID64.
class ISteamSessionTicketVerifier {
public:
    virtual ~ISteamSessionTicketVerifier() = default;

    virtual snt::core::Expected<VerifiedSteamAccount> verify_ticket(
        snt::network::PeerId peer, std::span<const std::byte> ticket,
        const snt::network::ReplicationTickContext& context) = 0;
};

// Dedicated servers inject this at startup. server_password is intentionally
// runtime-only and must never be loaded from a client-distributed package or
// emitted in logs. An empty value leaves the server open.
struct GameAccountPeerAuthenticatorConfig {
    ISteamSessionTicketVerifier* steam_ticket_verifier = nullptr;
    bool allow_local_name_accounts = true;
    std::string server_password;
};

class GameAccountPeerAuthenticator final : public IGamePeerAuthenticator {
public:
    explicit GameAccountPeerAuthenticator(GameAccountPeerAuthenticatorConfig config = {});

    [[nodiscard]] bool requires_server_password() const noexcept {
        return !server_password_.empty();
    }

    snt::core::Expected<GameAuthenticatedPeer> authenticate(
        snt::network::PeerId peer, const GameLoginRequest& request,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

private:
    ISteamSessionTicketVerifier* steam_ticket_verifier_ = nullptr;
    bool allow_local_name_accounts_ = true;
    std::string server_password_;
};

}  // namespace snt::game::replication
