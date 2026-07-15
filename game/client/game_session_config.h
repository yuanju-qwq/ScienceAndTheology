// ScienceAndTheology game-session configuration.
//
// These fields describe game content and behavior. They deliberately live in
// the host repository rather than the reusable engine runtime.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>

namespace snt::game {

struct GameCameraConfig {
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float move_speed = 4.3f;
    float look_speed = 0.1f;
    float initial_feet_position[3] = {4.0f, 6.0f, 8.0f};
};

struct GameSceneConfig {
    std::string path = "scenes/default_scene.bin";
    uint64_t active_camera_guid = 1;
};

struct GameScriptConfig {
    bool enabled = true;
    bool watch_for_changes = true;
    std::string root = "scripts";
};

struct GameDemoConfig {
    bool bootstrap_chunks = true;
    uint32_t seed = 20240601u;
};

// Universe-level gameplay persistence stays under the writable user root by
// default. The server composition resolves this path once at startup; it is
// never interpreted by fixed-tick systems or transport code.
struct GamePersistenceConfig {
    std::string universe_save_dir = "saves/default";
    bool world_save_enabled = false;
    std::string world_dimension_id = "overworld";
    std::string universe_mode = "default";
};

// Dedicated-server transport settings are data-only so the shared simulation
// target remains independent of snt_network. The server composition layer
// translates these fields into a concrete transport at startup.
struct GameServerNetworkConfig {
    bool enabled = false;
    std::string bind_address = "0.0.0.0";
    uint16_t tcp_port = 23585;
    uint16_t udp_port = 23586;
    uint32_t max_peers = 64;
};

// Graphical-client transport settings. Authentication evidence is not config:
// local-name credentials are empty, while a future Steamworks package injects
// its opaque ticket at startup and must never write it to game JSON or logs.
struct GameClientNetworkConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    uint16_t tcp_port = 23585;
    uint16_t udp_port = 23586;
};

struct GameSessionConfig {
    GameCameraConfig camera;
    GameSceneConfig scene;
    GameScriptConfig scripts;
    GameDemoConfig demo;
    GamePersistenceConfig persistence;
    GameServerNetworkConfig server_network;
    GameClientNetworkConfig client_network;
};

// Reads the game-owned subset from the same package JSON consumed by the
// runtime configuration loader. Unknown runtime keys are intentionally ignored.
snt::core::Expected<GameSessionConfig> load_game_session_config(const std::string& path);

}  // namespace snt::game
