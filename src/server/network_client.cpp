#include "network_client.hpp"

#include <cstring>
#include <utility>

namespace science_and_theology {
namespace server {

NetworkClient::NetworkClient() = default;

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t tcp_port,
                            uint16_t udp_port, const std::string& password) {
    if (state_ != ClientState::DISCONNECTED &&
        state_ != ClientState::REJECTED &&
        state_ != ClientState::DISCONNECTED_ERROR) {
        return false;  // already connecting/connected
    }

    server_host_ = host;
    server_tcp_port_ = tcp_port;
    server_udp_port_ = (udp_port == 0) ? static_cast<uint16_t>(tcp_port + 1) : udp_port;
    pending_password_ = password;

    // Resolve hostname and connect (blocking for M3; can be made async).
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(server_tcp_port_);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        set_state(ClientState::DISCONNECTED_ERROR);
        return false;
    }

    socket_t sock = kInvalidSocket;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        sock = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == kInvalidSocket) continue;
        if (::connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            break;  // connected
        }
        close_socket(sock);
        sock = kInvalidSocket;
    }
    freeaddrinfo(result);

    if (sock == kInvalidSocket) {
        set_state(ClientState::DISCONNECTED_ERROR);
        return false;
    }

    tcp_conn_ = TcpConnection(sock);

    // Bind a UDP socket for position updates (ephemeral port).
    if (!udp_socket_.bind(0)) {
        // TCP works without UDP; position updates just won't function.
        // Continue anyway.
    } else {
        udp_bound_ = true;
    }

    set_state(ClientState::LOGGING_IN);
    send_login(pending_password_);
    return true;
}

void NetworkClient::disconnect() {
    if (state_ == ClientState::DISCONNECTED) return;

    // Send graceful logout if connected.
    if (tcp_conn_.is_valid() &&
        (state_ == ClientState::CONNECTED || state_ == ClientState::LOGGING_IN)) {
        tcp_conn_.send_frame(PacketType::CMD_LOGOUT, player_id_, nullptr, 0);
    }

    tcp_conn_.close();
    if (udp_bound_) {
        udp_socket_.close();
        udp_bound_ = false;
    }
    player_id_ = 0;
    set_state(ClientState::DISCONNECTED);
}

void NetworkClient::poll(int timeout_ms) {
    (void)timeout_ms;  // M3: non-blocking poll, no select.

    // --- TCP recv + decode ---
    if (tcp_conn_.is_valid()) {
        while (true) {
            int n = tcp_conn_.recv_to_buffer();
            if (n > 0) continue;
            if (n == 0) {
                // Server closed the connection. Don't break immediately —
                // there may be pending frames (e.g. LOGIN_REJECT) in the
                // recv buffer that we still need to decode.
                break;
            }
            if (n == -2) {
                set_state(ClientState::DISCONNECTED_ERROR);
                tcp_conn_.close();
                break;
            }
            break;  // -1 = would block
        }

        if (tcp_conn_.is_valid()) {
            std::vector<Frame> frames;
            if (!tcp_conn_.drain_frames(frames)) {
                // Decode error.
                set_state(ClientState::DISCONNECTED_ERROR);
                tcp_conn_.close();
            } else {
                for (auto& frame : frames) {
                    handle_frame(frame);
                }
            }
            // If the peer closed and we're not in a terminal state,
            // transition to DISCONNECTED_ERROR (unless handle_frame
            // already set REJECTED/CONNECTED etc.).
            if (tcp_conn_.peer_closed() &&
                state_ != ClientState::REJECTED &&
                state_ != ClientState::DISCONNECTED) {
                set_state(ClientState::DISCONNECTED_ERROR);
                tcp_conn_.close();
            }
        }
    }

    // --- UDP recv (position updates from other players) ---
    if (udp_bound_) {
        while (auto gram = udp_socket_.recv_from()) {
            std::vector<uint8_t> buf = gram->data;
            auto result = decode_frame(buf);
            if (result.status != DecodeStatus::OK || !result.frame) continue;
            const Frame& frame = *result.frame;
            if (frame.type == PacketType::POS_UPDATE) {
                if (position_handler_) {
                    position_handler_(frame.player_id, frame.payload);
                }
            }
        }
    }

    // --- Discovery recv ---
    if (discovery_active_) {
        while (auto gram = discovery_socket_.recv_from()) {
            std::vector<uint8_t> buf = gram->data;
            auto result = decode_frame(buf);
            if (result.status != DecodeStatus::OK || !result.frame) continue;
            const Frame& frame = *result.frame;
            if (frame.type == PacketType::DISCOVER_REPLY) {
                DiscoveredServer ds;
                ds.ip = gram->from_ip;
                ds.port = gram->from_port;
                // Parse payload: [player_count:u16][name utf8]
                if (frame.payload.size() >= 2) {
                    ds.player_count = static_cast<uint16_t>(frame.payload[0]) |
                                      (static_cast<uint16_t>(frame.payload[1]) << 8);
                    ds.name = std::string(
                        reinterpret_cast<const char*>(frame.payload.data()) + 2,
                        frame.payload.size() - 2);
                }
                std::lock_guard<std::mutex> lock(discovered_mutex_);
                discovered_servers_.push_back(std::move(ds));
            }
        }
    }
}

bool NetworkClient::submit_command(const std::vector<uint8_t>& payload,
                                   uint64_t client_tick) {
    if (state_ != ClientState::CONNECTED) return false;
    if (!tcp_conn_.is_valid()) return false;

    // For M3, client_tick is not yet embedded in the payload; the host
    // is expected to include it in the serialized command payload.
    (void)client_tick;

    const int n = tcp_conn_.send_frame(PacketType::CMD_COMMAND, player_id_,
                                       payload.data(), payload.size());
    return n > 0;
}

bool NetworkClient::send_position_update(const std::vector<uint8_t>& payload) {
    if (state_ != ClientState::CONNECTED) return false;
    if (!udp_bound_) return false;

    auto wire = encode_frame(PacketType::POS_UPDATE, player_id_,
                             payload.data(), payload.size());
    return udp_socket_.send_to(server_host_, server_udp_port_, wire);
}

bool NetworkClient::start_discovery(uint16_t discovery_port) {
    if (!discovery_socket_.bind(0)) return false;
    discovery_socket_.enable_broadcast();
    discovery_active_ = true;

    auto wire = encode_frame_empty(PacketType::DISCOVER_REQ, 0);
    return discovery_socket_.broadcast(discovery_port, wire);
}

std::vector<DiscoveredServer> NetworkClient::drain_discovered_servers() {
    std::lock_guard<std::mutex> lock(discovered_mutex_);
    auto result = std::move(discovered_servers_);
    discovered_servers_.clear();
    return result;
}

void NetworkClient::set_sync_handler(ClientSyncHandler cb) {
    sync_handler_ = std::move(cb);
}

void NetworkClient::set_position_handler(ClientPositionHandler cb) {
    position_handler_ = std::move(cb);
}

void NetworkClient::set_state_handler(ClientStateHandler cb) {
    state_handler_ = std::move(cb);
}

void NetworkClient::set_state(ClientState new_state) {
    const ClientState old = state_.exchange(new_state);
    if (old != new_state && state_handler_) {
        state_handler_(new_state);
    }
}

void NetworkClient::handle_frame(const Frame& frame) {
    switch (frame.type) {
        case PacketType::LOGIN_ACCEPT:
            // Payload: [player_id:u64]
            if (frame.payload.size() >= 8) {
                player_id_ = 0;
                for (int i = 0; i < 8; ++i) {
                    player_id_ |= static_cast<uint64_t>(frame.payload[i]) << (i * 8);
                }
                set_state(ClientState::CONNECTED);
            }
            break;

        case PacketType::LOGIN_REJECT: {
            const std::string reason(
                reinterpret_cast<const char*>(frame.payload.data()),
                frame.payload.size());
            (void)reason;  // host can query state; reason logged by host
            set_state(ClientState::REJECTED);
            tcp_conn_.close();
            break;
        }

        case PacketType::SYNC_SNAPSHOT:
        case PacketType::SYNC_DELTA:
        case PacketType::SYNC_PLAYER_STATE:
            if (sync_handler_) {
                sync_handler_(frame.type, frame.payload);
            }
            break;

        case PacketType::POS_UPDATE:
            if (position_handler_) {
                position_handler_(frame.player_id, frame.payload);
            }
            break;

        case PacketType::KICK:
            set_state(ClientState::DISCONNECTED_ERROR);
            tcp_conn_.close();
            break;

        case PacketType::HEARTBEAT:
            // No action needed; heartbeat keeps the connection alive.
            break;

        default:
            break;
    }
}

void NetworkClient::send_login(const std::string& password) {
    // Payload: [password_len:u16][password bytes]
    std::vector<uint8_t> payload;
    const uint16_t pw_len = static_cast<uint16_t>(password.size());
    payload.push_back(static_cast<uint8_t>(pw_len & 0xFF));
    payload.push_back(static_cast<uint8_t>((pw_len >> 8) & 0xFF));
    payload.insert(payload.end(), password.begin(), password.end());

    tcp_conn_.send_frame(PacketType::CMD_LOGIN, 0,
                         payload.data(), payload.size());
}

} // namespace server
} // namespace science_and_theology
