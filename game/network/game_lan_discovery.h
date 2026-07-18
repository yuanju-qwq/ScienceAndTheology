// Game-facing LAN server discovery boundary.
//
// snt_network owns UDP broadcast framing. This module owns which discovered
// servers are compatible with the current game replication protocol, so UI
// and connection code never need to inspect engine discovery payloads.

#pragma once

#include "core/expected.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::network {
class LanDiscoveryClient;
}

namespace snt::game::replication {

inline constexpr uint16_t kDefaultGameLanDiscoveryPort = 23587;

struct GameLanDiscoveryClientConfig {
    std::string target_address = "255.255.255.255";
    uint16_t port = kDefaultGameLanDiscoveryPort;
};

// A compatible server endpoint ready for GameClientReplicationSession's
// direct TCP+UDP connection path. Password text is never advertised: clients
// receive only whether an optional server password is required.
struct GameLanDiscoveredServer {
    std::string host;
    std::string server_name;
    uint16_t tcp_port = 0;
    uint16_t udp_port = 0;
    uint16_t current_players = 0;
    uint16_t max_players = 0;
    bool password_required = false;
};

class GameLanDiscoveryClient final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameLanDiscoveryClient>> create(
        GameLanDiscoveryClientConfig config = {});

    ~GameLanDiscoveryClient();

    GameLanDiscoveryClient(const GameLanDiscoveryClient&) = delete;
    GameLanDiscoveryClient& operator=(const GameLanDiscoveryClient&) = delete;

    // Sends one broadcast query. Repeated calls are intentional when a UI
    // refresh window is open; poll() only returns compatible replies received
    // since its previous call.
    [[nodiscard]] snt::core::Expected<void> query();
    [[nodiscard]] snt::core::Expected<std::vector<GameLanDiscoveredServer>> poll();
    [[nodiscard]] uint16_t local_port() const noexcept;
    void shutdown() noexcept;

private:
    explicit GameLanDiscoveryClient(std::unique_ptr<snt::network::LanDiscoveryClient> client);

    std::unique_ptr<snt::network::LanDiscoveryClient> client_;
};

}  // namespace snt::game::replication
