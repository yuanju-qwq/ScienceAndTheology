#include "server_core.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace science_and_theology {
namespace server {

ServerCore::ServerCore() = default;

ServerCore::~ServerCore() {
    stop();
}

bool ServerCore::start(uint16_t tcp_port, uint16_t udp_port) {
    if (running_) return true;

    if (!tcp_listener_.listen(tcp_port)) {
        return false;
    }

    if (!udp_socket_.bind(udp_port)) {
        tcp_listener_.close();
        return false;
    }
    udp_socket_.enable_broadcast();
    udp_bound_ = true;

    running_ = true;
    return true;
}

void ServerCore::stop() {
    if (!running_) return;
    running_ = false;

    // Notify all clients of shutdown via KICK.
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& session : sessions_) {
            if (session->conn.is_valid() && session->logged_in) {
                const std::string reason = "server shutting down";
                session->conn.send_frame(PacketType::KICK, session->player_id,
                    reinterpret_cast<const uint8_t*>(reason.data()),
                    reason.size());
            }
            session->conn.close();
        }
        sessions_.clear();
    }

    tcp_listener_.close();
    if (udp_bound_) {
        udp_socket_.close();
        udp_bound_ = false;
    }
    command_queue_.clear();
}

void ServerCore::set_command_executor(CommandExecutor cb) {
    command_executor_ = std::move(cb);
}

void ServerCore::set_delta_producer(DeltaProducer cb) {
    delta_producer_ = std::move(cb);
}

void ServerCore::set_login_handler(LoginHandler cb) {
    login_handler_ = std::move(cb);
}

void ServerCore::set_disconnect_handler(DisconnectHandler cb) {
    disconnect_handler_ = std::move(cb);
}

size_t ServerCore::player_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    size_t count = 0;
    for (const auto& s : sessions_) {
        if (s->logged_in) ++count;
    }
    return count;
}

size_t ServerCore::session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
}

std::vector<uint64_t> ServerCore::logged_in_player_ids() const {
    std::vector<uint64_t> ids;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    ids.reserve(sessions_.size());
    for (const auto& s : sessions_) {
        if (s->logged_in) {
            ids.push_back(s->player_id);
        }
    }
    return ids;
}

uint64_t ServerCore::allocate_player_id() {
    // PlayerId 0 is invalid; ids start at 1.
    // Cap at kMaxPlayers (design §7 Q3: 20 players).
    return next_player_id_.fetch_add(1);
}

void ServerCore::poll(int timeout_ms) {
    if (!running_) return;

    // --- Phase 1: Accept new TCP connections ---
    while (auto conn = tcp_listener_.accept()) {
        auto session = std::make_unique<ClientSession>();
        session->conn = std::move(*conn);
        // Note: remote IP/port extraction omitted for brevity; can be
        // added via getpeername() if needed for logging.
        session->player_id = 0;  // not yet logged in
        session->logged_in = false;

        // Enforce player cap before accepting login (checked again in
        // handle_login). We still accept the TCP connection so we can
        // send a LOGIN_REJECT if the server is full.
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.push_back(std::move(session));
    }

    // --- Phase 2: Build fd_set for select ---
    // We poll the TCP listener (already non-blocking, so accept() above
    // suffices) and all client sockets + the UDP socket.
    // For simplicity in M3, we use non-blocking recv on each socket
    // rather than select(). This is O(n) in connections but fine for
    // kMaxPlayers = 20.

    // --- Phase 3: Recv + decode from all TCP sessions ---
    std::vector<Frame> frames;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& session : sessions_) {
            if (!session->conn.is_valid()) {
                session->marked_for_close = true;
                continue;
            }

            // Drain all available bytes.
            while (true) {
                int n = session->conn.recv_to_buffer();
                if (n > 0) continue;       // more data may be available
                if (n == 0) {              // peer closed
                    session->marked_for_close = true;
                    break;
                }
                if (n == -1) break;        // would block — done for now
                if (n == -2) {             // error
                    session->marked_for_close = true;
                    break;
                }
            }

            // Decode complete frames.
            if (!session->marked_for_close) {
                frames.clear();
                if (!session->conn.drain_frames(frames)) {
                    // Decode error — corrupt connection.
                    session->marked_for_close = true;
                } else {
                    for (auto& frame : frames) {
                        handle_frame(*session, frame);
                    }
                }
            }
        }
    }

    // --- Phase 4: Recv UDP datagrams (position + discovery) ---
    if (udp_bound_) {
        while (auto gram = udp_socket_.recv_from()) {
            handle_udp_datagram(*gram);
        }
    }

    // --- Phase 5: Drain CommandQueue + execute ---
    if (command_executor_) {
        std::vector<QueuedCommand> cmds;
        command_queue_.drain_all(cmds);
        for (auto& cmd : cmds) {
            auto response = command_executor_(cmd.player_id, cmd.client_tick, cmd.payload);
            if (!response.empty()) {
                send_to_player(cmd.player_id, PacketType::SYNC_DELTA, response);
            }
        }
    }

    // --- Phase 6: Produce deltas + send to clients ---
    if (delta_producer_) {
        auto deltas = delta_producer_();
        for (auto& [pid, payload] : deltas) {
            if (!payload.empty()) {
                send_to_player(pid, PacketType::SYNC_DELTA, payload);
            }
        }
    }

    // --- Phase 7: Cleanup closed sessions ---
    cleanup_sessions();
}

void ServerCore::handle_frame(ClientSession& session, const Frame& frame) {
    switch (frame.type) {
        case PacketType::CMD_LOGIN:
            handle_login(session, frame);
            break;

        case PacketType::CMD_COMMAND: {
            if (!session.logged_in) {
                // Reject: not logged in.
                std::string reason = "not logged in";
                session.conn.send_frame(PacketType::KICK, 0,
                    reinterpret_cast<const uint8_t*>(reason.data()),
                    reason.size());
                session.marked_for_close = true;
                return;
            }
            // Enqueue for tick-time execution.
            QueuedCommand cmd;
            cmd.player_id = session.player_id;
            cmd.client_tick = 0;  // TODO: extract from payload if present
            cmd.payload = frame.payload;
            command_queue_.push(std::move(cmd));
            break;
        }

        case PacketType::CMD_LOGOUT:
            session.marked_for_close = true;
            break;

        case PacketType::HEARTBEAT:
            // Echo back a heartbeat to keep the connection alive.
            session.conn.send_frame(PacketType::HEARTBEAT, session.player_id,
                                    nullptr, 0);
            break;

        default:
            // Unknown / unexpected packet type on TCP — ignore.
            break;
    }
}

void ServerCore::handle_login(ClientSession& session, const Frame& frame) {
    if (session.logged_in) {
        // Already logged in — reject duplicate login.
        send_login_reject(session, "already logged in");
        session.marked_for_close = true;
        return;
    }

    // Enforce player cap.
    if (count_logged_in_unlocked() >= kMaxPlayers) {
        send_login_reject(session, "server full");
        session.marked_for_close = true;
        return;
    }

    // Validate password if one is set.
    if (has_password()) {
        // Payload format for login: [password_len:u16][password bytes][extra...]
        // For M3 we do a simple prefix comparison.
        if (frame.payload.size() < 2) {
            send_login_reject(session, "missing credentials");
            session.marked_for_close = true;
            return;
        }
        const uint16_t pw_len = static_cast<uint16_t>(frame.payload[0]) |
                                (static_cast<uint16_t>(frame.payload[1]) << 8);
        if (frame.payload.size() < static_cast<size_t>(2 + pw_len)) {
            send_login_reject(session, "malformed credentials");
            session.marked_for_close = true;
            return;
        }
        const std::string provided(
            reinterpret_cast<const char*>(frame.payload.data()) + 2,
            pw_len);
        if (provided != password_) {
            send_login_reject(session, "invalid password");
            session.marked_for_close = true;
            return;
        }
    }

    // Allocate player_id.
    const uint64_t pid = allocate_player_id();
    session.player_id = pid;
    session.logged_in = true;

    // Let the host register the player.
    if (login_handler_) {
        std::string reject_reason;
        if (!login_handler_(pid, frame.payload, reject_reason)) {
            send_login_reject(session, reject_reason);
            session.logged_in = false;
            session.player_id = 0;
            session.marked_for_close = true;
            return;
        }
    }

    send_login_accept(session);
}

void ServerCore::handle_udp_datagram(const UdpDatagram& gram) {
    // Decode the datagram as a frame (UDP datagrams carry one frame each).
    std::vector<uint8_t> buf = gram.data;
    auto result = decode_frame(buf);
    if (result.status != DecodeStatus::OK || !result.frame) {
        return;  // ignore malformed UDP datagrams
    }

    const Frame& frame = *result.frame;
    switch (frame.type) {
        case PacketType::POS_UPDATE:
            // Relay to all other logged-in clients.
            broadcast_position(frame.player_id, frame.payload);
            break;

        case PacketType::DISCOVER_REQ:
            handle_discovery_request(gram);
            break;

        default:
            break;
    }
}

void ServerCore::handle_discovery_request(const UdpDatagram& gram) {
    // Reply with server name + player count so clients can display it.
    // Payload: [player_count:u16][server_name utf8]
    std::vector<uint8_t> reply_payload;
    const uint16_t count = static_cast<uint16_t>(player_count());
    reply_payload.push_back(static_cast<uint8_t>(count & 0xFF));
    reply_payload.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    reply_payload.insert(reply_payload.end(),
                         server_name_.begin(), server_name_.end());

    auto wire = encode_frame(PacketType::DISCOVER_REPLY, 0,
                             reply_payload.data(), reply_payload.size());
    udp_socket_.send_to(gram.from_ip, gram.from_port, wire);
}

void ServerCore::send_login_accept(ClientSession& session) {
    // Payload: [player_id:u64]
    std::vector<uint8_t> payload(8);
    for (int i = 0; i < 8; ++i) {
        payload[i] = static_cast<uint8_t>((session.player_id >> (i * 8)) & 0xFF);
    }
    session.conn.send_frame(PacketType::LOGIN_ACCEPT, session.player_id,
                            payload.data(), payload.size());
}

void ServerCore::send_login_reject(ClientSession& session, const std::string& reason) {
    session.conn.send_frame(PacketType::LOGIN_REJECT, 0,
        reinterpret_cast<const uint8_t*>(reason.data()),
        reason.size());
}

bool ServerCore::send_to_player(uint64_t player_id, PacketType type,
                                const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (session->logged_in && session->player_id == player_id) {
            if (!session->conn.is_valid()) return false;
            const int n = session->conn.send_frame(type, player_id,
                payload.data(), payload.size());
            return n > 0;
        }
    }
    return false;
}

void ServerCore::broadcast_position(uint64_t originator_player_id,
                                    const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (!session->logged_in) continue;
        if (session->player_id == originator_player_id) continue;  // don't echo back
        if (!session->conn.is_valid()) continue;
        session->conn.send_frame(PacketType::POS_UPDATE, originator_player_id,
            payload.data(), payload.size());
    }
}

void ServerCore::kick_player(uint64_t player_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (session->logged_in && session->player_id == player_id) {
            session->conn.send_frame(PacketType::KICK, player_id,
                reinterpret_cast<const uint8_t*>(reason.data()),
                reason.size());
            session->marked_for_close = true;
            break;
        }
    }
}

void ServerCore::cleanup_sessions() {
    std::vector<uint64_t> disconnected_ids;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& session : sessions_) {
            if (session->marked_for_close && !session->shutdown_sent) {
                // Graceful shutdown of the send side so pending frames
                // (e.g. LOGIN_REJECT, KICK) are delivered to the peer.
                // The socket is fully closed on the next poll cycle.
                if (session->conn.is_valid()) {
                    session->conn.shutdown_send();
                }
                session->shutdown_sent = true;
                if (session->logged_in) {
                    disconnected_ids.push_back(session->player_id);
                    session->logged_in = false;
                }
            }
        }
        // Fully close sessions that were already shut down last cycle.
        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                [](const std::unique_ptr<ClientSession>& s) {
                    if (s->shutdown_sent) {
                        s->conn.close();
                        return true;
                    }
                    return s->marked_for_close && !s->conn.is_valid();
                }),
            sessions_.end());
    }

    // Notify host of disconnections (outside the lock).
    if (disconnect_handler_) {
        for (uint64_t pid : disconnected_ids) {
            disconnect_handler_(pid);
        }
    }
}

ClientSession* ServerCore::find_session(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (session->logged_in && session->player_id == player_id) {
            return session.get();
        }
    }
    return nullptr;
}

size_t ServerCore::count_logged_in_unlocked() const {
    size_t count = 0;
    for (const auto& s : sessions_) {
        if (s->logged_in) ++count;
    }
    return count;
}

} // namespace server
} // namespace science_and_theology
