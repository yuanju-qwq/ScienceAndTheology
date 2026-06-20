#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>

#include <memory>

#include "server/network_client.hpp"

namespace science_and_theology {

// GDNetworkClient — Godot Node wrapping snt_server::NetworkClient.
//
// Design: docs/多人游戏系统设计.md §4 方案B (Godot 集成)
//
// The client-side node connects to an snt_server, submits commands,
// and receives sync deltas. The host (WorldMap.tscn in client mode)
// uses this node instead of a local GDGameCommandServer for authority.
//
// Usage in GDScript:
//   var net_client = GDNetworkClient.new()
//   net_client.connect_to_host("127.0.0.1", 8910)
//   # In _process:
//   net_client.poll()
//   # Submit a command:
//   net_client.submit_command({"type": "mine_block", ...})
class GDNetworkClient : public godot::Node {
    GDCLASS(GDNetworkClient, godot::Node)

public:
    GDNetworkClient();
    ~GDNetworkClient() override;

    // --- Connection ---

    // Connect to a server. password may be empty for open servers.
    // Returns true if the TCP connection was initiated.
    bool connect_to_host(const godot::String& host, int64_t tcp_port,
                         int64_t udp_port = 0,
                         const godot::String& password = "");

    // Disconnect from the server.
    void disconnect_from_host();

    // Poll for network activity. Call from _process() every frame.
    void poll();

    // --- Commands ---

    // Submit a gameplay command to the server.
    // The Dictionary is serialized via var_to_bytes and sent as a
    // CMD_COMMAND frame. Returns true if the frame was sent.
    bool submit_command(const godot::Dictionary& command);

    // Send a high-frequency position update (UDP).
    // The Dictionary is serialized and sent as a POS_UPDATE frame.
    bool send_position_update(const godot::Dictionary& pos_data);

    // --- LAN discovery ---

    // Broadcast a discovery request. Replies are collected by poll()
    // and emitted as "server_discovered" signals.
    bool start_discovery(int64_t discovery_port = 8912);

    // --- State ---

    // Returns the current connection state as a string:
    // "disconnected", "logging_in", "connected", "rejected", "error".
    godot::String get_state() const;

    bool is_connected() const;

    // The player_id assigned by the server (0 if not connected).
    int64_t get_player_id() const;

protected:
    static void _bind_methods();

private:
    // NetworkClient callbacks — bridge to Godot signals.
    void on_sync(server::PacketType type, const std::vector<uint8_t>& payload);
    void on_position(uint64_t player_id, const std::vector<uint8_t>& payload);
    void on_state_changed(server::ClientState new_state);

    // Serialize/deserialize Dictionary ↔ byte vector.
    static std::vector<uint8_t> dict_to_bytes(const godot::Dictionary& dict);
    static godot::Dictionary bytes_to_dict(const std::vector<uint8_t>& bytes);

    static godot::String state_to_string(server::ClientState s);

    std::unique_ptr<server::NetworkClient> client_;
};

} // namespace science_and_theology
