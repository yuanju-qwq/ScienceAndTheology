// Game-owned world persistence lifecycle implementation.

#define SNT_LOG_CHANNEL "game.world_persistence"
#include "game/world/save/world_persistence_lifecycle.h"

#include "core/error.h"
#include "core/log.h"
#include "game/world/game_chunk.h"
#include "game/world/save/save_manager.h"
#include "voxel/data/chunk_registry.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace snt::game {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error file_error(std::string message) {
    return {snt::core::ErrorCode::kFileOpenFailed, std::move(message)};
}

[[nodiscard]] bool is_safe_path_component(std::string_view value) {
    if (value.empty() || value == "." || value == "..") return false;
    const fs::path path(value);
    return !path.has_root_name() && !path.has_root_directory() && path.parent_path().empty();
}

[[nodiscard]] snt::core::Expected<bool> path_exists(const fs::path& path,
                                                     std::string_view label) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) {
        return file_error("Unable to inspect " + std::string(label) + ": " + error.message());
    }
    return exists;
}

}  // namespace

GameWorldPersistenceLifecycle::GameWorldPersistenceLifecycle(
    GameWorldPersistenceDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {}

snt::core::Expected<bool> GameWorldPersistenceLifecycle::load_existing(
    snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars) const {
    if (auto result = validate_descriptor(); !result) return result.error();
    if (chunks.chunk_count() != 0 || sidecars.size() != 0) {
        return invalid_state("Game world persistence load requires empty chunk and sidecar registries");
    }

    const fs::path universe_root(descriptor_.universe_save_dir);
    const fs::path universe_header = universe_root / "universe_header.bin";
    const fs::path planet_root = GameSaveManager::planet_dir(
        descriptor_.universe_save_dir, descriptor_.dimension_id);
    const fs::path planet_header = planet_root / "planet_data.bin";

    auto has_universe_header = path_exists(universe_header, "universe header");
    if (!has_universe_header) return has_universe_header.error();
    auto has_planet_header = path_exists(planet_header, "dimension header");
    if (!has_planet_header) return has_planet_header.error();

    if (!*has_universe_header) {
        if (*has_planet_header) {
            return invalid_state(
                "Game world has a dimension save without its universe header; refusing to overwrite it");
        }
        return false;
    }

    const auto [valid_header, stored_seed, stored_mode] =
        GameSaveManager::read_universe_header(descriptor_.universe_save_dir);
    if (!valid_header) {
        return invalid_state("Game world universe header is corrupt or not the current format");
    }
    if (stored_seed != descriptor_.seed) {
        return invalid_state("Game world seed does not match the configured current universe");
    }
    if (stored_mode != descriptor_.universe_mode) {
        return invalid_state("Game world mode does not match the configured current universe");
    }
    if (!*has_planet_header) {
        SNT_LOG_INFO("Loaded empty game universe '%s'; dimension '%s' has no region data yet",
                     descriptor_.universe_save_dir.c_str(), descriptor_.dimension_id.c_str());
        return true;
    }

    try {
        int64_t stored_dimension_seed = 0;
        std::string stored_dimension_id;
        GamePlanetSummaryData ignored_summary;
        bool ignored_has_summary = false;
        if (!GameSaveManager::read_planet_data(
                planet_root.string(), stored_dimension_seed, stored_dimension_id,
                ignored_summary, ignored_has_summary)) {
            return invalid_state("Game world dimension header is corrupt or not the current format");
        }
        if (stored_dimension_seed != descriptor_.seed ||
            stored_dimension_id != descriptor_.dimension_id) {
            return invalid_state(
                "Game world dimension header does not match the configured current universe");
        }
        const int loaded = GameSaveManager::load_dimension(
            planet_root.string(), descriptor_.dimension_id, chunks, sidecars);
        if (loaded < 0) {
            return invalid_state(
                "Game world dimension is corrupt or not the current format; refusing to bootstrap over it");
        }
        SNT_LOG_INFO("Loaded game world dimension '%s' from '%s' (%d chunk(s))",
                     descriptor_.dimension_id.c_str(), planet_root.string().c_str(), loaded);
    } catch (const std::exception& error) {
        return file_error("Game world dimension load failed: " + std::string(error.what()));
    } catch (...) {
        return file_error("Game world dimension load raised an unknown error");
    }
    return true;
}

snt::core::Expected<void> GameWorldPersistenceLifecycle::save(
    const snt::voxel::ChunkRegistry& chunks, const GameChunkSidecarRegistry& sidecars) const {
    if (auto result = validate_descriptor(); !result) return result.error();

    const std::string planet_root = GameSaveManager::planet_dir(
        descriptor_.universe_save_dir, descriptor_.dimension_id);
    try {
        const int saved = GameSaveManager::save_dimension(
            planet_root, descriptor_.seed, descriptor_.dimension_id, chunks, sidecars);
        if (saved < 0) return file_error("Game world dimension save failed");
        if (!GameSaveManager::write_universe_header(
                descriptor_.universe_save_dir, descriptor_.seed, descriptor_.universe_mode)) {
            return file_error("Game world universe header write failed after dimension save");
        }
        SNT_LOG_INFO("Saved game world dimension '%s' to '%s' (%d chunk(s))",
                     descriptor_.dimension_id.c_str(), planet_root.c_str(), saved);
    } catch (const std::exception& error) {
        return file_error("Game world dimension save failed: " + std::string(error.what()));
    } catch (...) {
        return file_error("Game world dimension save raised an unknown error");
    }
    return {};
}

snt::core::Expected<void> GameWorldPersistenceLifecycle::validate_descriptor() const {
    if (descriptor_.universe_save_dir.empty()) {
        return invalid_argument("Game world universe_save_dir must not be empty");
    }
    if (!is_safe_path_component(descriptor_.dimension_id)) {
        return invalid_argument("Game world dimension_id must be a single safe path component");
    }
    if (descriptor_.universe_mode.empty()) {
        return invalid_argument("Game world universe_mode must not be empty");
    }
    return {};
}

}  // namespace snt::game
