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
    // Dedicated-server player-progress autosave cadence. Zero leaves only the
    // explicit disconnect and controlled-shutdown persistence boundaries.
    uint64_t player_progress_autosave_interval_ticks = 1200;
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
    // LAN discovery is a separate IPv4 UDP query/reply socket. Its public
    // fields are safe to distribute with the game package; server passwords
    // remain runtime-only startup data and never appear in this config.
    bool lan_discovery_enabled = true;
    uint16_t lan_discovery_port = 23587;
    std::string lan_server_name = "ScienceAndTheology Server";
};

// Server-owned player state stays separate from camera/presentation settings.
// Current defaults establish spawn, fixed inventory capacity, interaction
// reach, and server-authoritative movement. Position intent remains a network
// input value; this configuration never grants a client a movement result.
struct GameServerPlayerConfig {
    int32_t spawn_block_x = 4;
    int32_t spawn_block_y = 6;
    int32_t spawn_block_z = 8;
    uint32_t inventory_slots = 36;
    int32_t inventory_max_stack_size = 64;
    int32_t interaction_reach_blocks = 5;
    float movement_walk_speed_blocks_per_second = 4.3f;
    float movement_sprint_multiplier = 1.45f;
    float movement_jump_speed_blocks_per_second = 6.2f;
    float movement_gravity_blocks_per_second_squared = 20.0f;
    float movement_terminal_velocity_blocks_per_second = 48.0f;
    float movement_body_width_blocks = 0.6f;
    float movement_body_height_blocks = 1.8f;
    uint64_t movement_input_timeout_ticks = 6;
    bool movement_missing_chunks_are_solid = true;
    // Reserved world material for the current-format indestructible grave
    // anchor. The content pack supplies its visual mapping separately.
    uint32_t grave_material_id = 255;
    uint32_t grave_vertical_search_blocks = 32;
    uint32_t respawn_safe_search_radius_blocks = 16;
};

// Per-observer player replication limits. These values are deliberately data
// rather than protocol constants so AOI coverage and throughput can be tuned
// from runtime configuration and performance tests.
struct GameServerReplicationConfig {
    uint32_t player_horizontal_aoi_radius_blocks = 96;
    uint32_t player_vertical_aoi_radius_blocks = 48;
    uint32_t max_visible_players = 64;
    uint32_t chunk_horizontal_aoi_radius_blocks = 64;
    uint32_t chunk_vertical_aoi_radius_blocks = 64;
    uint32_t max_visible_chunks = 8;
    uint32_t max_reliable_bytes_per_tick = 256u * 1024u;
    uint32_t max_chunk_snapshots_per_tick = 2;
    uint32_t max_entity_snapshots_per_tick = 128;
    uint32_t max_value_snapshots_per_tick = 32;
    uint32_t max_block_deltas_per_tick = 1024;
};

// Graphical-client transport settings. Authentication evidence is not config:
// local-name credentials are empty, while a future Steamworks package injects
// its opaque ticket at startup and must never write it to game JSON or logs.
struct GameClientNetworkConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    uint16_t tcp_port = 23585;
    uint16_t udp_port = 23586;
    // LAN browsing is opt-in so the graphical client keeps its current
    // offline startup path. When enabled, the client opens the native server
    // browser and creates TCP+UDP transport only after a server is selected.
    bool lan_discovery_enabled = false;
    std::string lan_discovery_address = "255.255.255.255";
    uint16_t lan_discovery_port = 23587;
};

struct GameSessionConfig {
    GameCameraConfig camera;
    GameSceneConfig scene;
    GameScriptConfig scripts;
    GameDemoConfig demo;
    GamePersistenceConfig persistence;
    GameServerNetworkConfig server_network;
    GameServerPlayerConfig server_player;
    GameServerReplicationConfig server_replication;
    GameClientNetworkConfig client_network;
};

// Reads the game-owned subset from the same package JSON consumed by the
// runtime configuration loader. Unknown runtime keys are intentionally ignored.
snt::core::Expected<GameSessionConfig> load_game_session_config(const std::string& path);

}  // namespace snt::game
