// Game-session configuration loader.

#define SNT_LOG_CHANNEL "game_config"
#include "game_session_config.h"

#include "core/log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace snt::game {
using json = nlohmann::json;

namespace {

template <typename T>
void read_optional(const json& object, const char* key, T& out) {
    if (object.contains(key)) {
        out = object[key].get<T>();
    }
}

}  // namespace

void from_json(const json& object, GameCameraConfig& value) {
    value = GameCameraConfig{};
    read_optional(object, "fov", value.fov);
    read_optional(object, "near_plane", value.near_plane);
    read_optional(object, "far_plane", value.far_plane);
    read_optional(object, "move_speed", value.move_speed);
    read_optional(object, "look_speed", value.look_speed);
    if (object.contains("initial_position") && object["initial_position"].is_array()) {
        const auto& positions = object["initial_position"];
        for (int index = 0; index < 3 && index < static_cast<int>(positions.size()); ++index) {
            value.initial_feet_position[index] = positions[index].get<float>();
        }
    }
}

void from_json(const json& object, GameSceneConfig& value) {
    value = GameSceneConfig{};
    read_optional(object, "path", value.path);
    read_optional(object, "active_camera_guid", value.active_camera_guid);
}

void from_json(const json& object, GameScriptConfig& value) {
    value = GameScriptConfig{};
    read_optional(object, "enabled", value.enabled);
    read_optional(object, "watch_for_changes", value.watch_for_changes);
    read_optional(object, "root", value.root);
}

void from_json(const json& object, GameDemoConfig& value) {
    value = GameDemoConfig{};
    read_optional(object, "bootstrap_chunks", value.bootstrap_chunks);
    read_optional(object, "seed", value.seed);
}

void from_json(const json& object, GameplayConfig::PlanetOverride& value) {
    value = GameplayConfig::PlanetOverride{};
    read_optional(object, "has_enable_collapse", value.has_enable_collapse);
    read_optional(object, "enable_collapse", value.enable_collapse);
    read_optional(object, "has_collapse_chance_multiplier",
                  value.has_collapse_chance_multiplier);
    read_optional(object, "collapse_chance_multiplier", value.collapse_chance_multiplier);
    read_optional(object, "has_max_collapse_chain", value.has_max_collapse_chain);
    read_optional(object, "max_collapse_chain", value.max_collapse_chain);
    read_optional(object, "has_support_beam_radius", value.has_support_beam_radius);
    read_optional(object, "support_beam_radius", value.support_beam_radius);
    read_optional(object, "has_enable_gravity_fall", value.has_enable_gravity_fall);
    read_optional(object, "enable_gravity_fall", value.enable_gravity_fall);
    read_optional(object, "has_max_gravity_fall_chain", value.has_max_gravity_fall_chain);
    read_optional(object, "max_gravity_fall_chain", value.max_gravity_fall_chain);
    read_optional(object, "has_enable_day_night", value.has_enable_day_night);
    read_optional(object, "enable_day_night", value.enable_day_night);
    read_optional(object, "has_day_length_seconds", value.has_day_length_seconds);
    read_optional(object, "day_length_seconds", value.day_length_seconds);
    read_optional(object, "has_twilight_fraction", value.has_twilight_fraction);
    read_optional(object, "twilight_fraction", value.twilight_fraction);
    read_optional(object, "has_day_start_time", value.has_day_start_time);
    read_optional(object, "day_start_time", value.day_start_time);
    read_optional(object, "has_enable_ecosystem", value.has_enable_ecosystem);
    read_optional(object, "enable_ecosystem", value.enable_ecosystem);
    read_optional(object, "has_ecosystem_rate_multiplier",
                  value.has_ecosystem_rate_multiplier);
    read_optional(object, "ecosystem_rate_multiplier", value.ecosystem_rate_multiplier);
}

void from_json(const json& object, GameplayConfig& value) {
    value = GameplayConfig{};
    read_optional(object, "enable_collapse", value.enable_collapse);
    read_optional(object, "collapse_chance_multiplier", value.collapse_chance_multiplier);
    read_optional(object, "max_collapse_chain", value.max_collapse_chain);
    read_optional(object, "support_beam_radius", value.support_beam_radius);
    read_optional(object, "enable_gravity_fall", value.enable_gravity_fall);
    read_optional(object, "max_gravity_fall_chain", value.max_gravity_fall_chain);
    read_optional(object, "enable_day_night", value.enable_day_night);
    read_optional(object, "day_length_seconds", value.day_length_seconds);
    read_optional(object, "twilight_fraction", value.twilight_fraction);
    read_optional(object, "day_start_time", value.day_start_time);
    read_optional(object, "days_per_season", value.days_per_season);
    read_optional(object, "enable_season_colors", value.enable_season_colors);
    read_optional(object, "enable_ecosystem", value.enable_ecosystem);
    read_optional(object, "ecosystem_rate_multiplier", value.ecosystem_rate_multiplier);
    if (object.contains("planet_overrides")) {
        value.planet_overrides = object.at("planet_overrides").get<
            std::unordered_map<std::string, GameplayConfig::PlanetOverride>>();
    }
}

void from_json(const json& object, GameServerNetworkConfig& value) {
    value = GameServerNetworkConfig{};
    read_optional(object, "enabled", value.enabled);
    read_optional(object, "bind_address", value.bind_address);
    read_optional(object, "tcp_port", value.tcp_port);
    read_optional(object, "udp_port", value.udp_port);
    read_optional(object, "max_peers", value.max_peers);
    read_optional(object, "lan_discovery_enabled", value.lan_discovery_enabled);
    read_optional(object, "lan_discovery_port", value.lan_discovery_port);
    read_optional(object, "lan_server_name", value.lan_server_name);
}

void from_json(const json& object, GameServerPlayerConfig& value) {
    value = GameServerPlayerConfig{};
    read_optional(object, "spawn_block_x", value.spawn_block_x);
    read_optional(object, "spawn_block_y", value.spawn_block_y);
    read_optional(object, "spawn_block_z", value.spawn_block_z);
    read_optional(object, "inventory_slots", value.inventory_slots);
    read_optional(object, "inventory_max_stack_size", value.inventory_max_stack_size);
    read_optional(object, "interaction_reach_blocks", value.interaction_reach_blocks);
    read_optional(object, "movement_walk_speed_blocks_per_second",
                  value.movement_walk_speed_blocks_per_second);
    read_optional(object, "movement_sprint_multiplier", value.movement_sprint_multiplier);
    read_optional(object, "movement_jump_speed_blocks_per_second",
                  value.movement_jump_speed_blocks_per_second);
    read_optional(object, "movement_gravity_blocks_per_second_squared",
                  value.movement_gravity_blocks_per_second_squared);
    read_optional(object, "movement_terminal_velocity_blocks_per_second",
                  value.movement_terminal_velocity_blocks_per_second);
    read_optional(object, "movement_body_width_blocks", value.movement_body_width_blocks);
    read_optional(object, "movement_body_height_blocks", value.movement_body_height_blocks);
    read_optional(object, "movement_input_timeout_ticks", value.movement_input_timeout_ticks);
    read_optional(object, "movement_missing_chunks_are_solid",
                  value.movement_missing_chunks_are_solid);
    read_optional(object, "grave_material_id", value.grave_material_id);
    read_optional(object, "grave_vertical_search_blocks", value.grave_vertical_search_blocks);
    read_optional(object, "respawn_safe_search_radius_blocks",
                  value.respawn_safe_search_radius_blocks);
}

void from_json(const json& object, GameServerReplicationConfig& value) {
    value = GameServerReplicationConfig{};
    read_optional(object, "player_horizontal_aoi_radius_blocks",
                  value.player_horizontal_aoi_radius_blocks);
    read_optional(object, "player_vertical_aoi_radius_blocks",
                  value.player_vertical_aoi_radius_blocks);
    read_optional(object, "max_visible_players", value.max_visible_players);
    read_optional(object, "chunk_horizontal_aoi_radius_blocks",
                  value.chunk_horizontal_aoi_radius_blocks);
    read_optional(object, "chunk_vertical_aoi_radius_blocks",
                  value.chunk_vertical_aoi_radius_blocks);
    read_optional(object, "max_visible_chunks", value.max_visible_chunks);
    read_optional(object, "creature_horizontal_aoi_radius_blocks",
                  value.creature_horizontal_aoi_radius_blocks);
    read_optional(object, "creature_vertical_aoi_radius_blocks",
                  value.creature_vertical_aoi_radius_blocks);
    read_optional(object, "max_visible_creature_chunks", value.max_visible_creature_chunks);
    read_optional(object, "max_visible_creatures", value.max_visible_creatures);
    read_optional(object, "max_reliable_bytes_per_tick", value.max_reliable_bytes_per_tick);
    read_optional(object, "max_chunk_snapshots_per_tick", value.max_chunk_snapshots_per_tick);
    read_optional(object, "max_entity_snapshots_per_tick", value.max_entity_snapshots_per_tick);
    read_optional(object, "max_value_snapshots_per_tick", value.max_value_snapshots_per_tick);
    read_optional(object, "max_block_deltas_per_tick", value.max_block_deltas_per_tick);
}

void from_json(const json& object, GamePersistenceConfig& value) {
    value = GamePersistenceConfig{};
    read_optional(object, "universe_save_dir", value.universe_save_dir);
    read_optional(object, "world_save_enabled", value.world_save_enabled);
    read_optional(object, "world_dimension_id", value.world_dimension_id);
    read_optional(object, "universe_mode", value.universe_mode);
    read_optional(object, "player_progress_autosave_interval_ticks",
                  value.player_progress_autosave_interval_ticks);
    read_optional(object, "ground_loot_despawn_after_ticks",
                  value.ground_loot_despawn_after_ticks);
    read_optional(object, "ground_loot_lifecycle_sweep_interval_ticks",
                  value.ground_loot_lifecycle_sweep_interval_ticks);
}

void from_json(const json& object, GameClientNetworkConfig& value) {
    value = GameClientNetworkConfig{};
    read_optional(object, "enabled", value.enabled);
    read_optional(object, "host", value.host);
    read_optional(object, "tcp_port", value.tcp_port);
    read_optional(object, "udp_port", value.udp_port);
    read_optional(object, "lan_discovery_enabled", value.lan_discovery_enabled);
    read_optional(object, "lan_discovery_address", value.lan_discovery_address);
    read_optional(object, "lan_discovery_port", value.lan_discovery_port);
}

void from_json(const json& object, GameClientInteractionConfig& value) {
    value = GameClientInteractionConfig{};
    read_optional(object, "raycast_reach_blocks", value.raycast_reach_blocks);
    read_optional(object, "bed_material_id", value.bed_material_id);
    read_optional(object, "ignition_item_id", value.ignition_item_id);
    read_optional(object, "fertilizer_item_id", value.fertilizer_item_id);
}

void from_json(const json& object, GameSessionConfig& value) {
    value = GameSessionConfig{};
    if (object.contains("camera")) value.camera = object["camera"].get<GameCameraConfig>();
    if (object.contains("scene")) value.scene = object["scene"].get<GameSceneConfig>();
    if (object.contains("scripts")) value.scripts = object["scripts"].get<GameScriptConfig>();
    if (object.contains("demo")) value.demo = object["demo"].get<GameDemoConfig>();
    if (object.contains("gameplay")) value.gameplay = object["gameplay"].get<GameplayConfig>();
    if (object.contains("persistence")) {
        value.persistence = object["persistence"].get<GamePersistenceConfig>();
    }
    if (object.contains("server_network")) {
        value.server_network = object["server_network"].get<GameServerNetworkConfig>();
    }
    if (object.contains("server_player")) {
        value.server_player = object["server_player"].get<GameServerPlayerConfig>();
    }
    if (object.contains("server_replication")) {
        value.server_replication = object["server_replication"].get<GameServerReplicationConfig>();
    }
    if (object.contains("client_network")) {
        value.client_network = object["client_network"].get<GameClientNetworkConfig>();
    }
    if (object.contains("client_interaction")) {
        value.client_interaction = object["client_interaction"].get<GameClientInteractionConfig>();
    }
}

snt::core::Expected<GameSessionConfig> load_game_session_config(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        SNT_LOG_INFO("Game-session config '%s' not found; using defaults", path.c_str());
        return GameSessionConfig{};
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    try {
        const json object = json::parse(buffer.str());
        SNT_LOG_INFO("Game-session config loaded from '%s'", path.c_str());
        return object.get<GameSessionConfig>();
    } catch (const std::exception& error) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string("Game-session config JSON parse error: ") + error.what()};
    }
}

}  // namespace snt::game
