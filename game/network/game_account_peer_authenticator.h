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

class GameAccountPeerAuthenticator final : public IGamePeerAuthenticator {
public:
    explicit GameAccountPeerAuthenticator(ISteamSessionTicketVerifier* steam_ticket_verifier = nullptr,
                                          bool allow_local_name_accounts = true);

    snt::core::Expected<GameAuthenticatedPeer> authenticate(
        snt::network::PeerId peer, const GameLoginRequest& request,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

private:
    ISteamSessionTicketVerifier* steam_ticket_verifier_ = nullptr;
    bool allow_local_name_accounts_ = true;
};

}  // namespace snt::game::replication
