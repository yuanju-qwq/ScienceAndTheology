#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

#include "server/server_core.hpp"

namespace science_and_theology {

class GDGameCommandServer;
class GDTickSystem;

// GDNetworkServer — Godot Node wrapping snt_server::ServerCore.
//
// Legacy Godot integration; current multiplayer protocol: docs/游戏网络协议设计.md.
//
// The host (WorldMap.tscn) adds this node alongside GDGameCommandServer
// and GDTickSystem. It bridges network frames to/from the existing
// command execution and state sync logic:
//
//   CMD_COMMAND (from client)
//     → deserialize Dictionary via bytes_to_var
//     → GDGameCommandServer.submit_command()
//     → serialize result Dictionary via var_to_bytes
//     → send SYNC_DELTA back to that client
//
//   Per-tick delta production:
//     → GDTickSystem.compute_delta_for(player_handle, chunk_keys)
//     → serialize delta Dictionary via var_to_bytes
//     → send SYNC_DELTA to that client
//
// Usage in GDScript:
//   var net_server = GDNetworkServer.new()
//   net_server.set_command_server($GameCommandServer)
//   net_server.set_tick_system($TickSystem)
//   net_server.set_password("optional")
//   net_server.start(8910)
//   # In _process:
//   net_server.poll()
class GDNetworkServer : public godot::Node {
    GDCLASS(GDNetworkServer, godot::Node)

public:
    GDNetworkServer();
    ~GDNetworkServer() override;

    // --- Lifecycle ---

    // Start listening. tcp_port = command channel, udp_port = position channel.
    // Returns true on success.
    bool start(int64_t tcp_port = 8910, int64_t udp_port = 8911);

    // Stop the server and disconnect all clients.
    void stop();

    // Poll for network activity. Call from _process() every frame.
    void poll();

    bool is_running() const;

    // --- Configuration ---

    // Set the GDGameCommandServer that executes gameplay commands.
    void set_command_server(godot::Node* cmd_server);
    godot::Node* get_command_server() const;

    // Set the GDTickSystem used for delta production.
    void set_tick_system(godot::Node* tick_sys);
    godot::Node* get_tick_system() const;

    // Optional password (empty = open server).
    void set_password(const godot::String& pw);
    godot::String get_password() const;

    // Server name reported in LAN discovery replies.
    void set_server_name(const godot::String& name);
    godot::String get_server_name() const;

    // --- Queries ---

    // Number of logged-in players.
    int64_t get_player_count() const;

    // Number of TCP sessions (including not-yet-logged-in).
    int64_t get_session_count() const;

    // Returns the player IDs of all currently logged-in players.
    // Use this instead of assuming sequential IDs 1..N (M5).
    godot::PackedInt64Array get_logged_in_player_handles() const;

    // Kick a player by id.
    void kick_player(int64_t player_handle, const godot::String& reason);

protected:
    static void _bind_methods();

private:
    // ServerCore callbacks — bridge to Godot types.
    std::vector<uint8_t> on_command(uint64_t player_handle, uint64_t client_tick,
                                    const std::vector<uint8_t>& payload);
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> on_produce_deltas();
    bool on_login(uint64_t player_handle, const std::vector<uint8_t>& credentials,
                  std::string& reject_reason);
    void on_disconnect(uint64_t player_handle);

    // Serialize/deserialize Dictionary ↔ byte vector.
    static std::vector<uint8_t> dict_to_bytes(const godot::Dictionary& dict);
    static godot::Dictionary bytes_to_dict(const std::vector<uint8_t>& bytes);

    std::unique_ptr<server::ServerCore> core_;
    GDGameCommandServer* command_server_ = nullptr;
    GDTickSystem* tick_system_ = nullptr;
    godot::String password_;
    godot::String server_name_ = "ScienceAndTheology Server";
};

} // namespace science_and_theology
