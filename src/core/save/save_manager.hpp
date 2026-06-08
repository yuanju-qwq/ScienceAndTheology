#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../world/world_data.hpp"
#include "region_file.hpp"

namespace science_and_theology {

// Manages save/load of an entire world to/from disk.
// Each save is a directory containing a world_header.bin and a regions/ folder.
//
// Directory structure:
//   {save_dir}/
//     world_header.bin
//     regions/
//       {layer}~{region_x}~{region_y}.region
//
// Each region file groups 32×32 chunks (1024 max) to reduce file system
// overhead and improve I/O locality during bulk loads.
//
// Usage:
//   SaveManager::save_world("saves/my_world", 12345, world);
//   auto [ok, seed] = SaveManager::load_world("saves/my_world", world);
class SaveManager {
public:
    // Current world header format version.
    static constexpr uint8_t kWorldHeaderVersion = 1;

    // Saves the entire world to disk using region files.
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
    // Ensures a directory exists, creating it if necessary.
    static bool ensure_directory(const std::string& path);
};

} // namespace science_and_theology