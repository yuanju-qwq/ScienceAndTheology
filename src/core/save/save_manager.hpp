#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../world/world_data.hpp"
#include "chunk_serializer.hpp"

namespace science_and_theology {

// Manages save/load of an entire world to/from disk.
// Each save is a directory containing a world_header.bin and a chunks/ folder.
//
// Directory structure:
//   {save_dir}/
//     world_header.bin
//     chunks/
//       {layer}~{chunk_x}~{chunk_y}.bin
//
// Usage:
//   SaveManager::save_world("saves/my_world", 12345, world);
//   auto [ok, seed] = SaveManager::load_world("saves/my_world", world);
class SaveManager {
public:
    // Current world header format version.
    static constexpr uint8_t kWorldHeaderVersion = 1;

    // Saves the entire world to disk.
    // Returns the number of chunks saved, or -1 on error.
    static int save_world(const std::string& save_dir,
                          int64_t seed, const WorldData& world);

    // Loads the entire world from disk.
    // Returns {success, seed}. On failure, success = false.
    // Existing chunks in the WorldData are cleared before loading.
    static std::pair<bool, int64_t> load_world(
        const std::string& save_dir, WorldData& world);

    // Writes only the world header (seed + version) to a save directory.
    // Called once when a save is first created.
    static bool write_world_header(const std::string& save_dir, int64_t seed);

    // Reads the world header. Returns {success, seed}.
    static std::pair<bool, int64_t> read_world_header(
        const std::string& save_dir);

    // Lists available save names in a base directory.
    // Filters out non-directory entries and entries without a world_header.bin.
    static std::vector<std::string> list_saves(
        const std::string& base_saves_dir);

private:
    // Builds a chunk file name from layer ID and coordinates.
    static std::string chunk_file_name(const std::string& layer_id,
                                       int chunk_x, int chunk_y);

    // Ensures a directory exists, creating it if necessary.
    static bool ensure_directory(const std::string& path);
};

} // namespace science_and_theology