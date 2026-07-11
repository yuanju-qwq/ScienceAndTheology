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

void from_json(const json& object, GameSessionConfig& value) {
    value = GameSessionConfig{};
    if (object.contains("camera")) value.camera = object["camera"].get<GameCameraConfig>();
    if (object.contains("scene")) value.scene = object["scene"].get<GameSceneConfig>();
    if (object.contains("scripts")) value.scripts = object["scripts"].get<GameScriptConfig>();
    if (object.contains("demo")) value.demo = object["demo"].get<GameDemoConfig>();
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
