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

godot::PackedInt64Array GDNetworkServer::get_logged_in_player_handles() const {
    godot::PackedInt64Array arr;
    if (!core_) return arr;
    auto ids = core_->logged_in_player_handles();
    arr.resize(static_cast<int64_t>(ids.size()));
    auto* ptr = arr.ptrw();
    for (size_t i = 0; i < ids.size(); ++i) {
        ptr[i] = static_cast<int64_t>(ids[i]);
    }
    return arr;
}

void GDNetworkServer::kick_player(int64_t player_handle, const String& reason) {
    if (core_) {
        core_->kick_player(static_cast<uint64_t>(player_handle),
                          reason.utf8().get_data());
    }
}

// --- Callbacks ---

std::vector<uint8_t> GDNetworkServer::on_command(
    uint64_t player_handle, uint64_t client_tick,
    const std::vector<uint8_t>& payload) {
    (void)client_tick;  // client_tick flows through the command Dictionary
    if (!command_server_) return {};

    // Deserialize the command Dictionary from the payload.
    godot::Dictionary cmd = bytes_to_dict(payload);
    if (cmd.is_empty()) return {};

    // Ensure the command carries the correct player_handle.
    cmd["player_handle"] = static_cast<int64_t>(player_handle);

    // Execute the command via the authoritative command server.
    godot::Dictionary result = command_server_->submit_command(cmd);

    // M6: Echo client_tick back so the client can reconcile predictions.
    // The client includes client_tick in the command Dictionary; the
    // command server may or may not echo it, so we ensure it here.
    if (cmd.has("client_tick")) {
        result["client_tick"] = cmd["client_tick"];
    }

    // Serialize the result back to bytes.
    return dict_to_bytes(result);
}

std::vector<std::pair<uint64_t, std::vector<uint8_t>>>
GDNetworkServer::on_produce_deltas() {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> deltas;
    if (!core_ || !tick_system_) return deltas;

    // M5: Enumerate actual logged-in player IDs instead of assuming
    // sequential 1..N (players may join/leave in any order, leaving
    // gaps in the ID space).
    auto player_handles = core_->logged_in_player_handles();
    if (player_handles.empty()) return deltas;

    // Get all dirty chunks from the tick system.
    godot::Array dirty = tick_system_->get_dirty_chunks();
    if (dirty.is_empty()) return deltas;

    // M5: Per-observer delta filtering by dimension.
    // Players on different planets only receive deltas for their own
    // planet. Design §3.6: 客户端只关心自己所在星球。
    //
    // Build per-observer chunk views (filtered by dimension), then use
    // the batch delta API so dirty flags are only cleared after ALL
    // observers have been processed. This ensures multiple players on
    // the same planet all receive the delta (the single-observer
    // compute_delta_for would clear flags after the first observer).
    godot::Array observer_views;
    for (uint64_t pid : player_handles) {
        const int64_t player_handle = static_cast<int64_t>(pid);
        godot::String player_dim = tick_system_->get_player_dimension(player_handle);
        if (player_dim.is_empty()) continue;  // player not registered yet

        // Filter dirty chunks to only those in the player's dimension.
        godot::Array player_dirty;
        for (int64_t i = 0; i < dirty.size(); ++i) {
            const godot::Variant& v = dirty[i];
            if (v.get_type() != godot::Variant::DICTIONARY) continue;
            godot::Dictionary chunk_dict = v;
            godot::String chunk_dim = chunk_dict["dimension"];
            if (chunk_dim == player_dim) {
                player_dirty.append(chunk_dict);
            }
        }
        if (player_dirty.is_empty()) continue;

        godot::Dictionary view;
        view["player_handle"] = player_handle;
        view["chunks"] = player_dirty;
        observer_views.append(view);
    }

    if (observer_views.is_empty()) return deltas;

    // Compute all deltas in batch (dirty flags cleared once at the end).
    godot::Array batch_results = tick_system_->compute_deltas_batch(observer_views);
    for (int64_t i = 0; i < batch_results.size(); ++i) {
        godot::Dictionary entry = batch_results[i];
        int64_t pid = entry["player_handle"];
        godot::Dictionary delta = entry["delta"];
        if (!delta.is_empty()) {
            deltas.emplace_back(static_cast<uint64_t>(pid), dict_to_bytes(delta));
        }
    }
    return deltas;
}

bool GDNetworkServer::on_login(uint64_t player_handle,
                               const std::vector<uint8_t>& credentials,
                               std::string& reject_reason) {
    (void)credentials;
    if (!command_server_) {
        reject_reason = "command server not configured";
        return false;
    }
    UtilityFunctions::print("[GDNetworkServer] player ", player_handle, " connected");
    emit_signal("player_connected", static_cast<int64_t>(player_handle));
    return true;
}

void GDNetworkServer::on_disconnect(uint64_t player_handle) {
    if (command_server_) {
        command_server_->unregister_player(static_cast<int64_t>(player_handle));
    }
    UtilityFunctions::print("[GDNetworkServer] player ", player_handle, " disconnected");
    emit_signal("player_disconnected", static_cast<int64_t>(player_handle));
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
    ClassDB::bind_method(D_METHOD("get_logged_in_player_handles"),
                         &GDNetworkServer::get_logged_in_player_handles);

    ClassDB::bind_method(D_METHOD("kick_player", "player_handle", "reason"),
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
                          PropertyInfo(Variant::INT, "player_handle")));
    ADD_SIGNAL(MethodInfo("player_disconnected",
                          PropertyInfo(Variant::INT, "player_handle")));
}

} // namespace science_and_theology
