#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "protocol/protocol_types.hpp"
#include "transport/socket_platform.hpp"
#include "transport/tcp_connection.hpp"
#include "transport/udp_socket.hpp"

namespace science_and_theology {
namespace server {

// Connection state of the client.
enum class ClientState {
    DISCONNECTED,
    CONNECTING,    // TCP connect in progress
    LOGGING_IN,    // TCP connected, waiting for LOGIN_ACCEPT
    CONNECTED,     // logged in, player_id assigned
    REJECTED,      // login was rejected
    DISCONNECTED_ERROR,  // dropped due to error
};

// A discovered LAN server.
struct DiscoveredServer {
    std::string ip;
    uint16_t port = 0;
    std::string name;
    uint16_t player_count = 0;
};

// Host-provided callbacks for the client.

// Called when a SYNC_DELTA / SYNC_SNAPSHOT frame arrives. The host
// decodes the payload and applies it to the local WorldData.
using ClientSyncHandler = std::function<void(
    PacketType type,
    const std::vector<uint8_t>& payload)>;

// Called when a POS_UPDATE frame arrives (another player's position).
using ClientPositionHandler = std::function<void(
    uint64_t player_id,
    const std::vector<uint8_t>& payload)>;

// Called when the connection state changes.
using ClientStateHandler = std::function<void(ClientState new_state)>;

// NetworkClient — client-side connection to an snt_server.
//
// Threading: all operations happen on the thread that calls poll().
// The host (GDNetworkClient) calls poll() from its _process() loop.
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    // Connect to a server. Performs a non-blocking TCP connect.
    // The connection completes asynchronously; call poll() to advance.
    // password may be empty for open servers.
    bool connect(const std::string& host, uint16_t tcp_port,
                 uint16_t udp_port = 0,  // 0 = tcp_port + 1
                 const std::string& password = "");

    // Disconnect from the server.
    void disconnect();

    // Poll for network activity. Call from the host's main loop.
    void poll(int timeout_ms = 0);

    // Send a gameplay command to the server.
    // Returns true if the frame was queued for sending.
    bool submit_command(const std::vector<uint8_t>& payload,
                        uint64_t client_tick = 0);

    // Send a position update to the server (UDP, unreliable).
    bool send_position_update(const std::vector<uint8_t>& payload);

    // --- LAN discovery ---

    // Broadcast a DISCOVER_REQ on the local subnet. Replies are
    // collected by poll() and returned by drain_discovered_servers().
    bool start_discovery(uint16_t discovery_port = kDefaultDiscoveryPort);

    // Returns and clears the list of servers discovered since the last call.
    std::vector<DiscoveredServer> drain_discovered_servers();

    // --- State ---

    ClientState state() const { return state_; }
    bool is_connected() const { return state_ == ClientState::CONNECTED; }
    uint64_t player_id() const { return player_id_; }

    // --- Host callbacks ---

    void set_sync_handler(ClientSyncHandler cb);
    void set_position_handler(ClientPositionHandler cb);
    void set_state_handler(ClientStateHandler cb);

private:
    void set_state(ClientState new_state);
    void handle_frame(const Frame& frame);
    void send_login(const std::string& password);

    // Socket library RAII.
    SocketLib socket_lib_;

    // TCP connection to the server.
    TcpConnection tcp_conn_;
    // UDP socket for position updates + discovery.
    UdpSocket udp_socket_;
    bool udp_bound_ = false;

    // Server address (for UDP position sends).
    std::string server_host_;
    uint16_t server_tcp_port_ = 0;
    uint16_t server_udp_port_ = 0;

    // Client state.
    std::atomic<ClientState> state_{ClientState::DISCONNECTED};
    uint64_t player_id_ = 0;
    std::string pending_password_;

    // Discovery socket (separate from the position UDP socket so the
    // client can discover servers before connecting).
    UdpSocket discovery_socket_;
    bool discovery_active_ = false;
    std::vector<DiscoveredServer> discovered_servers_;
    mutable std::mutex discovered_mutex_;

    // Host callbacks.
    ClientSyncHandler sync_handler_;
    ClientPositionHandler position_handler_;
    ClientStateHandler state_handler_;
};

} // namespace server
} // namespace science_and_theology
