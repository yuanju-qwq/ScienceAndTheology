#include "save_manager.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "chunk_serializer.hpp"

namespace fs = std::filesystem;

namespace science_and_theology {

int SaveManager::save_world(const std::string& save_dir,
                            int64_t seed, const WorldData& world) {
    if (!ensure_directory(save_dir)) {
        return -1;
    }

    std::string regions_dir = save_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return -1;
    }

    // Write world header.
    if (!write_world_header(save_dir, seed)) {
        return -1;
    }

    // Group chunks by {dimension_id, region_x, region_y, region_z}.
    std::map<std::string, std::vector<RegionChunkEntry>> region_chunks;
    std::map<std::string, std::tuple<int, int, int, std::string>> region_meta;

    for (const auto& key : world.all_chunk_keys()) {
        const ChunkData* chunk = world.get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr) {
            continue;
        }

        int rx = RegionFile::to_region(key.chunk_x);
        int ry = RegionFile::to_region(key.chunk_y);
        int rz = RegionFile::to_region(key.chunk_z);

        std::string file_name = RegionFile::region_file_name(
            key.dimension_id, rx, ry, rz);

        std::vector<uint8_t> data = ChunkSerializer::serialize(
            key.dimension_id, *chunk);

        RegionChunkEntry entry;
        entry.local_x = static_cast<uint8_t>(RegionFile::to_local(key.chunk_x));
        entry.local_y = static_cast<uint8_t>(RegionFile::to_local(key.chunk_y));
        entry.local_z = static_cast<uint8_t>(RegionFile::to_local(key.chunk_z));
        entry.data = std::move(data);

        region_chunks[file_name].push_back(std::move(entry));
        region_meta[file_name] = std::make_tuple(rx, ry, rz, key.dimension_id);
    }

    // Write each region file.
    int saved_count = 0;
    for (auto& [file_name, chunks] : region_chunks) {
        auto& [rx, ry, rz, dimension_id] = region_meta[file_name];
        std::string file_path = regions_dir + "/" + file_name;

        if (!RegionFile::write(file_path, rx, ry, rz, dimension_id, chunks)) {
            return -1;
        }

        saved_count += static_cast<int>(chunks.size());
    }

    return saved_count;
}

std::pair<bool, int64_t> SaveManager::load_world(
    const std::string& save_dir, WorldData& world) {
    // Read world header.
    auto [header_ok, seed] = read_world_header(save_dir);
    if (!header_ok) {
        return {false, 0};
    }

    world.clear();

    std::string regions_dir = save_dir + "/regions";
    if (!fs::exists(regions_dir) || !fs::is_directory(regions_dir)) {
        return {true, seed};
    }

    // Load each region file.
    for (const auto& entry : fs::directory_iterator(regions_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string file_path = entry.path().string();

        std::string dimension_id;
        int region_x, region_y, region_z;
        std::vector<RegionChunkEntry> entries;

        if (!RegionFile::read(file_path, dimension_id,
                              region_x, region_y, region_z, entries)) {
            continue;
        }

        for (auto& chunk_entry : entries) {
            int chunk_x = region_x * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_x);
            int chunk_y = region_y * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_y);
            int chunk_z = region_z * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_z);

            std::string unused_dimension_id;
            ChunkData chunk;
            if (!ChunkSerializer::deserialize(chunk_entry.data,
                                              unused_dimension_id, chunk)) {
                continue;
            }

            world.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z, std::move(chunk));
        }
    }

    return {true, seed};
}

bool SaveManager::write_world_header(const std::string& save_dir,
                                     int64_t seed) {
    if (!ensure_directory(save_dir)) {
        return false;
    }

    std::string header_path = save_dir + "/world_header.bin";
    std::ofstream file(header_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Version.
    uint8_t version = kWorldHeaderVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Seed.
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));

    // Reserved.
    int32_t reserved = 0;
    file.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

    file.close();
    return file.good();
}

std::pair<bool, int64_t> SaveManager::read_world_header(
    const std::string& save_dir) {
    std::string header_path = save_dir + "/world_header.bin";

    std::ifstream file(header_path, std::ios::binary);
    if (!file.is_open()) {
        return {false, 0};
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) return {false, 0};
    if (version != kWorldHeaderVersion) return {false, 0};

    int64_t seed;
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    if (!file.good()) return {false, 0};

    return {true, seed};
}

std::vector<std::string> SaveManager::list_saves(
    const std::string& base_saves_dir) {
    std::vector<std::string> result;

    if (!fs::exists(base_saves_dir) || !fs::is_directory(base_saves_dir)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(base_saves_dir)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string save_path = entry.path().string();
        std::string header_path = save_path + "/world_header.bin";

        if (fs::exists(header_path) && fs::is_regular_file(header_path)) {
            result.push_back(entry.path().filename().string());
        }
    }

    return result;
}

bool SaveManager::ensure_directory(const std::string& path) {
    if (fs::exists(path)) {
        return fs::is_directory(path);
    }

    std::error_code ec;
    bool created = fs::create_directories(path, ec);
    return created && !ec;
}

} // namespace science_and_theology
