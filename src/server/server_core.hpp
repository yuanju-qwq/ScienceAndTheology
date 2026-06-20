#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "command_queue.hpp"
#include "protocol/protocol_types.hpp"
#include "transport/socket_platform.hpp"
#include "transport/tcp_connection.hpp"
#include "transport/udp_socket.hpp"

namespace science_and_theology {
namespace server {

// A connected client session.
struct ClientSession {
    uint64_t player_id = 0;
    TcpConnection conn;
    std::string remote_ip;
    uint16_t remote_port = 0;
    bool logged_in = false;
    bool marked_for_close = false;
    // Set after shutdown(SHUT_WR) is called, to gracefully flush pending
    // sends before closesocket(). The session is fully closed one poll
    // cycle later, giving the peer time to recv the final frames.
    bool shutdown_sent = false;
};

// Host-provided callbacks. ServerCore handles transport + session + protocol;
// the host (GDNetworkServer) plugs in the actual gameplay logic.

// Execute a gameplay command. Called from the tick thread after draining
// the CommandQueue. Returns the response payload to send back to the
// submitting client (may be empty if no response is needed).
using CommandExecutor = std::function<std::vector<uint8_t>(
    uint64_t player_id,
    uint64_t client_tick,
    const std::vector<uint8_t>& payload)>;

// Produce per-client sync deltas for this tick. Called once per tick.
// Returns a list of (player_id, payload) pairs; ServerCore sends each
// payload to the matching client as a SYNC_DELTA frame.
using DeltaProducer = std::function<std::vector<std::pair<uint64_t, std::vector<uint8_t>>>()>;

// Called when a player logs in (after player_id is assigned).
// The host can register the player in PlayerManager here.
// Return true to accept, false to reject (with reason).
using LoginHandler = std::function<bool(
    uint64_t player_id,
    const std::vector<uint8_t>& credentials,
    std::string& reject_reason)>;

// Called when a player disconnects (graceful or error).
// The host can unregister the player from PlayerManager here.
using DisconnectHandler = std::function<void(uint64_t player_id)>;

// ServerCore — the authoritative server's network + session orchestrator.
//
// Threading model:
//   - The network poll loop runs on the thread that calls poll_network().
//     Typically this is the host's _process() thread (main thread).
//   - Command execution + delta production happens in the same poll call
//     (single-threaded for M3; can be moved to a dedicated thread later).
//
// This class is engine-agnostic: it never touches Godot types. The host
// (GDNetworkServer) provides the callbacks that bridge to gameplay.
class ServerCore {
public:
    ServerCore();
    ~ServerCore();

    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;

    // Start listening. Returns true on success.
    // tcp_port: command + reliable sync channel.
    // udp_port: high-frequency position channel.
    bool start(uint16_t tcp_port = kDefaultTcpPort,
               uint16_t udp_port = kDefaultUdpPort);

    // Stop the server, closing all connections.
    void stop();

    bool is_running() const { return running_; }

    // --- Network poll (call from host's main loop) ---
    //
    // Performs:
    //   1. Accept new TCP connections.
    //   2. Recv from all TCP connections, decode frames.
    //   3. Recv UDP datagrams (position updates + discovery).
    //   4. Drain CommandQueue, execute commands via callback.
    //   5. Produce deltas via callback, send to clients.
    //   6. Clean up closed/corrupt connections.
    //
    // timeout_ms: if >0, blocks for up to this long waiting for activity
    // (via select). 0 = non-blocking.
    void poll(int timeout_ms = 0);

    // --- Host callback registration ---

    void set_command_executor(CommandExecutor cb);
    void set_delta_producer(DeltaProducer cb);
    void set_login_handler(LoginHandler cb);
    void set_disconnect_handler(DisconnectHandler cb);

    // --- Server info ---

    // Number of currently connected (logged-in) players.
    size_t player_count() const;

    // Number of TCP sessions (including not-yet-logged-in).
    size_t session_count() const;

    // Set the server name reported in LAN discovery replies.
    void set_server_name(const std::string& name) { server_name_ = name; }
    const std::string& server_name() const { return server_name_; }

    // Set an optional password. If non-empty, clients must send it in
    // CMD_LOGIN. Empty = open server (design §7 Q1: 支持可选密码).
    void set_password(const std::string& pw) { password_ = pw; }
    bool has_password() const { return !password_.empty(); }

    // Send a SYNC_DELTA / SYNC_SNAPSHOT frame to a specific player.
    // Used by the host when it produces deltas outside the normal
    // DeltaProducer callback (e.g. immediate snapshot on login).
    bool send_to_player(uint64_t player_id, PacketType type,
                        const std::vector<uint8_t>& payload);

    // Broadcast a player position update (POS_UPDATE) to all clients
    // except the originator. The host calls this when it receives a
    // position update from one client and wants to relay it.
    void broadcast_position(uint64_t originator_player_id,
                            const std::vector<uint8_t>& payload);

    // Kick a player (sends KICK frame then closes the connection).
    void kick_player(uint64_t player_id, const std::string& reason);

private:
    // Assign the next available player_id. PlayerId 0 is invalid;
    // ids start at 1 (matching kSinglePlayerId convention).
    uint64_t allocate_player_id();

    // Handle a decoded frame from a specific session.
    void handle_frame(ClientSession& session, const Frame& frame);

    // Handle CMD_LOGIN: validate credentials, assign player_id, send reply.
    void handle_login(ClientSession& session, const Frame& frame);

    // Handle a UDP datagram (position update or discovery request).
    void handle_udp_datagram(const UdpDatagram& gram);

    // Send a LOGIN_ACCEPT frame.
    void send_login_accept(ClientSession& session);
    void send_login_reject(ClientSession& session, const std::string& reason);

    // Process discovery requests (LAN broadcast).
    void handle_discovery_request(const UdpDatagram& gram);

    // Clean up sessions marked for close.
    void cleanup_sessions();

    // Find a session by player_id. Returns nullptr if not found.
    ClientSession* find_session(uint64_t player_id);

    // Count logged-in sessions WITHOUT locking (caller must hold sessions_mutex_).
    // Used by handle_login which is called from within the poll() lock.
    size_t count_logged_in_unlocked() const;

    // Socket library RAII (winsock init on Windows).
    SocketLib socket_lib_;

    // Listening sockets.
    TcpListener tcp_listener_;
    UdpSocket udp_socket_;
    bool udp_bound_ = false;

    // Active client sessions, keyed by player_id (0 = not yet logged in,
    // stored in a separate list).
    std::vector<std::unique_ptr<ClientSession>> sessions_;
    mutable std::mutex sessions_mutex_;

    // Next player_id to assign.
    std::atomic<uint64_t> next_player_id_{1};

    // Command queue (filled by network, drained by tick).
    CommandQueue command_queue_;

    // Host callbacks.
    CommandExecutor command_executor_;
    DeltaProducer delta_producer_;
    LoginHandler login_handler_;
    DisconnectHandler disconnect_handler_;

    // Server configuration.
    std::string server_name_ = "ScienceAndTheology Server";
    std::string password_;
    bool running_ = false;
};

} // namespace server
} // namespace science_and_theology
