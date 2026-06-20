#include "gd_network_client.hpp"

#include <cstring>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace science_and_theology {

using namespace godot;

GDNetworkClient::GDNetworkClient() {
    client_ = std::make_unique<server::NetworkClient>();

    client_->set_sync_handler(
        [this](server::PacketType type, const std::vector<uint8_t>& payload) {
            on_sync(type, payload);
        });
    client_->set_position_handler(
        [this](uint64_t pid, const std::vector<uint8_t>& payload) {
            on_position(pid, payload);
        });
    client_->set_state_handler(
        [this](server::ClientState s) {
            on_state_changed(s);
        });
}

GDNetworkClient::~GDNetworkClient() {
    if (client_) {
        client_->disconnect();
    }
}

bool GDNetworkClient::connect_to_host(const String& host, int64_t tcp_port,
                                      int64_t udp_port,
                                      const String& password) {
    if (!client_) return false;
    return client_->connect(host.utf8().get_data(),
                            static_cast<uint16_t>(tcp_port),
                            static_cast<uint16_t>(udp_port),
                            password.utf8().get_data());
}

void GDNetworkClient::disconnect_from_host() {
    if (client_) client_->disconnect();
}

void GDNetworkClient::poll() {
    if (client_) client_->poll(0);
}

bool GDNetworkClient::submit_command(const Dictionary& command) {
    if (!client_ || !client_->is_connected()) return false;
    return client_->submit_command(dict_to_bytes(command));
}

bool GDNetworkClient::send_position_update(const Dictionary& pos_data) {
    if (!client_ || !client_->is_connected()) return false;
    return client_->send_position_update(dict_to_bytes(pos_data));
}

bool GDNetworkClient::start_discovery(int64_t discovery_port) {
    if (!client_) return false;
    return client_->start_discovery(static_cast<uint16_t>(discovery_port));
}

String GDNetworkClient::get_state() const {
    if (!client_) return "disconnected";
    return state_to_string(client_->state());
}

bool GDNetworkClient::is_connected() const {
    return client_ && client_->is_connected();
}

int64_t GDNetworkClient::get_player_id() const {
    return client_ ? static_cast<int64_t>(client_->player_id()) : 0;
}

// --- Callbacks ---

void GDNetworkClient::on_sync(server::PacketType type,
                              const std::vector<uint8_t>& payload) {
    godot::Dictionary data = bytes_to_dict(payload);
    // Emit "sync_received" with the packet type name and decoded data.
    String type_name;
    switch (type) {
        case server::PacketType::SYNC_SNAPSHOT:     type_name = "snapshot"; break;
        case server::PacketType::SYNC_DELTA:        type_name = "delta"; break;
        case server::PacketType::SYNC_PLAYER_STATE: type_name = "player_state"; break;
        default: type_name = "unknown"; break;
    }
    emit_signal("sync_received", type_name, data);
}

void GDNetworkClient::on_position(uint64_t player_id,
                                  const std::vector<uint8_t>& payload) {
    godot::Dictionary data = bytes_to_dict(payload);
    emit_signal("position_received", static_cast<int64_t>(player_id), data);
}

void GDNetworkClient::on_state_changed(server::ClientState new_state) {
    String state_str = state_to_string(new_state);
    emit_signal("state_changed", state_str);

    if (new_state == server::ClientState::CONNECTED) {
        emit_signal("connected", get_player_id());
    } else if (new_state == server::ClientState::REJECTED ||
               new_state == server::ClientState::DISCONNECTED_ERROR) {
        emit_signal("disconnected", state_str);
    }
}

// --- Serialization helpers ---

std::vector<uint8_t> GDNetworkClient::dict_to_bytes(const godot::Dictionary& dict) {
    PackedByteArray arr = UtilityFunctions::var_to_bytes(dict);
    const auto* ptr = reinterpret_cast<const uint8_t*>(arr.ptr());
    return std::vector<uint8_t>(ptr, ptr + arr.size());
}

godot::Dictionary GDNetworkClient::bytes_to_dict(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return godot::Dictionary();
    PackedByteArray arr;
    arr.resize(static_cast<int64_t>(bytes.size()));
    auto* ptr = reinterpret_cast<uint8_t*>(arr.ptrw());
    std::memcpy(ptr, bytes.data(), bytes.size());
    Variant v = UtilityFunctions::bytes_to_var(arr);
    if (v.get_type() != Variant::DICTIONARY) return godot::Dictionary();
    return v;
}

String GDNetworkClient::state_to_string(server::ClientState s) {
    switch (s) {
        case server::ClientState::DISCONNECTED:         return "disconnected";
        case server::ClientState::CONNECTING:            return "connecting";
        case server::ClientState::LOGGING_IN:            return "logging_in";
        case server::ClientState::CONNECTED:             return "connected";
        case server::ClientState::REJECTED:              return "rejected";
        case server::ClientState::DISCONNECTED_ERROR:    return "error";
        default: return "unknown";
    }
}

// --- Godot bindings ---

void GDNetworkClient::_bind_methods() {
    ClassDB::bind_method(D_METHOD("connect_to_host", "host", "tcp_port", "udp_port", "password"),
                         &GDNetworkClient::connect_to_host,
                         DEFVAL(0), DEFVAL(""));
    ClassDB::bind_method(D_METHOD("disconnect_from_host"),
                         &GDNetworkClient::disconnect_from_host);
    ClassDB::bind_method(D_METHOD("poll"), &GDNetworkClient::poll);

    ClassDB::bind_method(D_METHOD("submit_command", "command"),
                         &GDNetworkClient::submit_command);
    ClassDB::bind_method(D_METHOD("send_position_update", "pos_data"),
                         &GDNetworkClient::send_position_update);

    ClassDB::bind_method(D_METHOD("start_discovery", "discovery_port"),
                         &GDNetworkClient::start_discovery, DEFVAL(8912));

    ClassDB::bind_method(D_METHOD("get_state"), &GDNetworkClient::get_state);
    ClassDB::bind_method(D_METHOD("is_connected"), &GDNetworkClient::is_connected);
    ClassDB::bind_method(D_METHOD("get_player_id"), &GDNetworkClient::get_player_id);

    ADD_SIGNAL(MethodInfo("sync_received",
                          PropertyInfo(Variant::STRING, "type"),
                          PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("position_received",
                          PropertyInfo(Variant::INT, "player_id"),
                          PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("state_changed",
                          PropertyInfo(Variant::STRING, "state")));
    ADD_SIGNAL(MethodInfo("connected",
                          PropertyInfo(Variant::INT, "player_id")));
    ADD_SIGNAL(MethodInfo("disconnected",
                          PropertyInfo(Variant::STRING, "reason")));
    ADD_SIGNAL(MethodInfo("server_discovered",
                          PropertyInfo(Variant::STRING, "ip"),
                          PropertyInfo(Variant::INT, "port"),
                          PropertyInfo(Variant::STRING, "name"),
                          PropertyInfo(Variant::INT, "player_count")));
}

} // namespace science_and_theology
