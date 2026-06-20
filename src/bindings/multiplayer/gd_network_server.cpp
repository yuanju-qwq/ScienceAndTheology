#include "gd_network_server.hpp"

#include <cstring>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/array.hpp>

#include "bindings/simulation/gd_game_command_server.h"
#include "bindings/simulation/gd_tick_system.h"

namespace science_and_theology {

using namespace godot;

GDNetworkServer::GDNetworkServer() {
    core_ = std::make_unique<server::ServerCore>();
}

GDNetworkServer::~GDNetworkServer() {
    if (core_) {
        core_->stop();
    }
}

bool GDNetworkServer::start(int64_t tcp_port, int64_t udp_port) {
    if (!core_) return false;
    core_->set_server_name(server_name_.utf8().get_data());
    if (!password_.is_empty()) {
        core_->set_password(password_.utf8().get_data());
    }

    // Register callbacks.
    core_->set_command_executor(
        [this](uint64_t pid, uint64_t tick, const std::vector<uint8_t>& payload)
        -> std::vector<uint8_t> {
            return on_command(pid, tick, payload);
        });
    core_->set_delta_producer(
        [this]() -> std::vector<std::pair<uint64_t, std::vector<uint8_t>>> {
            return on_produce_deltas();
        });
    core_->set_login_handler(
        [this](uint64_t pid, const std::vector<uint8_t>& creds,
               std::string& reason) -> bool {
            return on_login(pid, creds, reason);
        });
    core_->set_disconnect_handler(
        [this](uint64_t pid) {
            on_disconnect(pid);
        });

    return core_->start(static_cast<uint16_t>(tcp_port),
                        static_cast<uint16_t>(udp_port));
}

void GDNetworkServer::stop() {
    if (core_) core_->stop();
}

void GDNetworkServer::poll() {
    if (core_) core_->poll(0);
}

bool GDNetworkServer::is_running() const {
    return core_ && core_->is_running();
}

void GDNetworkServer::set_command_server(Node* cmd_server) {
    command_server_ = Object::cast_to<GDGameCommandServer>(cmd_server);
    if (cmd_server != nullptr && command_server_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDNetworkServer: command_server is not a GDGameCommandServer");
    }
}

Node* GDNetworkServer::get_command_server() const {
    return command_server_;
}

void GDNetworkServer::set_tick_system(Node* tick_sys) {
    tick_system_ = Object::cast_to<GDTickSystem>(tick_sys);
    if (tick_sys != nullptr && tick_system_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDNetworkServer: tick_system is not a GDTickSystem");
    }
}

Node* GDNetworkServer::get_tick_system() const {
    return tick_system_;
}

void GDNetworkServer::set_password(const String& pw) {
    password_ = pw;
}

String GDNetworkServer::get_password() const {
    return password_;
}

void GDNetworkServer::set_server_name(const String& name) {
    server_name_ = name;
    if (core_) {
        core_->set_server_name(name.utf8().get_data());
    }
}

String GDNetworkServer::get_server_name() const {
    return server_name_;
}

int64_t GDNetworkServer::get_player_count() const {
    return core_ ? static_cast<int64_t>(core_->player_count()) : 0;
}

int64_t GDNetworkServer::get_session_count() const {
    return core_ ? static_cast<int64_t>(core_->session_count()) : 0;
}

void GDNetworkServer::kick_player(int64_t player_id, const String& reason) {
    if (core_) {
        core_->kick_player(static_cast<uint64_t>(player_id),
                          reason.utf8().get_data());
    }
}

// --- Callbacks ---

std::vector<uint8_t> GDNetworkServer::on_command(
    uint64_t player_id, uint64_t client_tick,
    const std::vector<uint8_t>& payload) {
    (void)client_tick;
    if (!command_server_) return {};

    // Deserialize the command Dictionary from the payload.
    godot::Dictionary cmd = bytes_to_dict(payload);
    if (cmd.is_empty()) return {};

    // Ensure the command carries the correct player_id.
    cmd["player_id"] = static_cast<int64_t>(player_id);

    // Execute the command via the authoritative command server.
    godot::Dictionary result = command_server_->submit_command(cmd);

    // Serialize the result back to bytes.
    return dict_to_bytes(result);
}

std::vector<std::pair<uint64_t, std::vector<uint8_t>>>
GDNetworkServer::on_produce_deltas() {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> deltas;
    if (!core_ || !tick_system_) return deltas;

    // For each connected player, compute their delta.
    // In M3 we use a simple approach: compute delta over all dirty chunks
    // for each player. Per-observer dirty tracking is M5+ scope.
    const int64_t player_count = core_->player_count();
    if (player_count == 0) return deltas;

    // Get all dirty chunks from the tick system.
    godot::Array dirty = tick_system_->get_dirty_chunks();
    if (dirty.is_empty()) return deltas;

    // For each player, compute delta for the dirty chunks.
    // TODO (M5): filter chunks by each player's view distance.
    // For now, all players see all dirty chunks.
    for (int64_t i = 1; i <= player_count; ++i) {
        godot::Dictionary delta = tick_system_->compute_delta_for(i, dirty);
        if (!delta.is_empty()) {
            deltas.emplace_back(static_cast<uint64_t>(i), dict_to_bytes(delta));
        }
    }
    return deltas;
}

bool GDNetworkServer::on_login(uint64_t player_id,
                               const std::vector<uint8_t>& credentials,
                               std::string& reject_reason) {
    (void)credentials;
    if (!command_server_) {
        reject_reason = "command server not configured";
        return false;
    }
    // The player will be registered in PlayerManager when they submit
    // their first command with their inventory/equipment. For now,
    // just accept. The host GDScript can override this by registering
    // the player explicitly on the "player_connected" signal.
    UtilityFunctions::print("[GDNetworkServer] player ", player_id, " connected");
    return true;
}

void GDNetworkServer::on_disconnect(uint64_t player_id) {
    if (command_server_) {
        command_server_->unregister_player(static_cast<int64_t>(player_id));
    }
    UtilityFunctions::print("[GDNetworkServer] player ", player_id, " disconnected");
}

// --- Serialization helpers ---

std::vector<uint8_t> GDNetworkServer::dict_to_bytes(const godot::Dictionary& dict) {
    PackedByteArray arr = UtilityFunctions::var_to_bytes(dict);
    const auto* ptr = reinterpret_cast<const uint8_t*>(arr.ptr());
    return std::vector<uint8_t>(ptr, ptr + arr.size());
}

godot::Dictionary GDNetworkServer::bytes_to_dict(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return godot::Dictionary();
    PackedByteArray arr;
    arr.resize(static_cast<int64_t>(bytes.size()));
    auto* ptr = reinterpret_cast<uint8_t*>(arr.ptrw());
    std::memcpy(ptr, bytes.data(), bytes.size());
    Variant v = UtilityFunctions::bytes_to_var(arr);
    if (v.get_type() != Variant::DICTIONARY) return godot::Dictionary();
    return v;
}

// --- Godot bindings ---

void GDNetworkServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start", "tcp_port", "udp_port"),
                         &GDNetworkServer::start, DEFVAL(8910), DEFVAL(8911));
    ClassDB::bind_method(D_METHOD("stop"), &GDNetworkServer::stop);
    ClassDB::bind_method(D_METHOD("poll"), &GDNetworkServer::poll);
    ClassDB::bind_method(D_METHOD("is_running"), &GDNetworkServer::is_running);

    ClassDB::bind_method(D_METHOD("set_command_server", "cmd_server"),
                         &GDNetworkServer::set_command_server);
    ClassDB::bind_method(D_METHOD("get_command_server"),
                         &GDNetworkServer::get_command_server);

    ClassDB::bind_method(D_METHOD("set_tick_system", "tick_sys"),
                         &GDNetworkServer::set_tick_system);
    ClassDB::bind_method(D_METHOD("get_tick_system"),
                         &GDNetworkServer::get_tick_system);

    ClassDB::bind_method(D_METHOD("set_password", "pw"),
                         &GDNetworkServer::set_password);
    ClassDB::bind_method(D_METHOD("get_password"),
                         &GDNetworkServer::get_password);

    ClassDB::bind_method(D_METHOD("set_server_name", "name"),
                         &GDNetworkServer::set_server_name);
    ClassDB::bind_method(D_METHOD("get_server_name"),
                         &GDNetworkServer::get_server_name);

    ClassDB::bind_method(D_METHOD("get_player_count"),
                         &GDNetworkServer::get_player_count);
    ClassDB::bind_method(D_METHOD("get_session_count"),
                         &GDNetworkServer::get_session_count);

    ClassDB::bind_method(D_METHOD("kick_player", "player_id", "reason"),
                         &GDNetworkServer::kick_player);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "command_server",
                              PROPERTY_HINT_NODE_TYPE, "GDGameCommandServer"),
                 "set_command_server", "get_command_server");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tick_system",
                              PROPERTY_HINT_NODE_TYPE, "GDTickSystem"),
                 "set_tick_system", "get_tick_system");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "password"),
                 "set_password", "get_password");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "server_name"),
                 "set_server_name", "get_server_name");

    ADD_SIGNAL(MethodInfo("player_connected",
                          PropertyInfo(Variant::INT, "player_id")));
    ADD_SIGNAL(MethodInfo("player_disconnected",
                          PropertyInfo(Variant::INT, "player_id")));
}

} // namespace science_and_theology
