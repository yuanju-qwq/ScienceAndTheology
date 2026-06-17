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

// --- Per-dimension save / load ---

int SaveManager::save_dimension(const std::string& planet_dir,
                                int64_t seed,
                                const std::string& dimension_id,
                                const WorldData& world) {
    if (!ensure_directory(planet_dir)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return -1;
    }

    // Write planet_data.bin header (no summary during save_dimension).
    if (!write_planet_data(planet_dir, seed, dimension_id, nullptr)) {
        return -1;
    }

    // Group chunks for this dimension by region.
    std::map<std::string, std::vector<RegionChunkEntry>> region_chunks;
    std::map<std::string, std::tuple<int, int, int, std::string>> region_meta;

    for (const auto& key : world.all_chunk_keys()) {
        if (key.dimension_id != dimension_id) {
            continue;
        }

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
        auto& [rx, ry, rz, dim] = region_meta[file_name];
        std::string file_path = regions_dir + "/" + file_name;

        if (!RegionFile::write(file_path, rx, ry, rz, dim, chunks)) {
            return -1;
        }

        saved_count += static_cast<int>(chunks.size());
    }

    return saved_count;
}

int SaveManager::load_dimension(const std::string& planet_dir,
                                const std::string& dimension_id,
                                WorldData& world) {
    // Read planet_data.bin to validate.
    int64_t seed = 0;
    std::string dim_id;
    PlanetSummaryData summary;
    bool has_summary = false;
    if (!read_planet_data(planet_dir, seed, dim_id, summary, has_summary)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    if (!fs::exists(regions_dir) || !fs::is_directory(regions_dir)) {
        return 0;
    }

    // Load each region file in this planet's directory.
    int loaded_count = 0;
    for (const auto& entry : fs::directory_iterator(regions_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string file_path = entry.path().string();

        std::string file_dimension_id;
        int region_x, region_y, region_z;
        std::vector<RegionChunkEntry> entries;

        if (!RegionFile::read(file_path, file_dimension_id,
                              region_x, region_y, region_z, entries)) {
            continue;
        }

        if (file_dimension_id != dimension_id) {
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

            world.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z,
                            std::move(chunk));
            loaded_count++;
        }
    }

    return loaded_count;
}

// --- Planet data (header + summary) ---

void SaveManager::write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) {
        file.write(str.data(), len);
    }
}

bool SaveManager::read_string(std::ifstream& file, std::string& out,
                               uint32_t max_len) {
    uint32_t len;
    file.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!file.good() || len > max_len) {
        return false;
    }
    out.resize(len);
    if (len > 0) {
        file.read(out.data(), len);
    }
    return file.good();
}

bool SaveManager::write_planet_data(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id,
                                     const PlanetSummaryData* summary) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }

    std::string path = planet_dir + "/planet_data.bin";
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Header section.
    uint8_t version = kPlanetDataVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, dimension_id);

    // Summary section.
    uint8_t has_summary = (summary != nullptr) ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&has_summary), sizeof(has_summary));

    if (summary != nullptr) {
        file.write(reinterpret_cast<const char*>(&summary->captured_tick),
                   sizeof(summary->captured_tick));

        // Production lines.
        uint32_t pl_count = static_cast<uint32_t>(summary->production_lines.size());
        file.write(reinterpret_cast<const char*>(&pl_count), sizeof(pl_count));
        for (const auto& line : summary->production_lines) {
            write_string(file, line.recipe_key);
            file.write(reinterpret_cast<const char*>(&line.rate_per_minute),
                       sizeof(line.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&line.active_count),
                       sizeof(line.active_count));
        }

        // Mining sites.
        uint32_t ms_count = static_cast<uint32_t>(summary->mining_sites.size());
        file.write(reinterpret_cast<const char*>(&ms_count), sizeof(ms_count));
        for (const auto& site : summary->mining_sites) {
            write_string(file, site.ore_key);
            file.write(reinterpret_cast<const char*>(&site.rate_per_minute),
                       sizeof(site.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&site.remaining_approx),
                       sizeof(site.remaining_approx));
        }

        // Storage levels.
        uint32_t sl_count = static_cast<uint32_t>(summary->storage_levels.size());
        file.write(reinterpret_cast<const char*>(&sl_count), sizeof(sl_count));
        for (const auto& entry : summary->storage_levels) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.count),
                       sizeof(entry.count));
            file.write(reinterpret_cast<const char*>(&entry.capacity),
                       sizeof(entry.capacity));
        }

        // Power summary.
        file.write(reinterpret_cast<const char*>(&summary->power_consumption_mw),
                   sizeof(summary->power_consumption_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_generation_mw),
                   sizeof(summary->power_generation_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_surplus_mw),
                   sizeof(summary->power_surplus_mw));

        // Accumulated production.
        uint32_t ap_count = static_cast<uint32_t>(summary->accumulated_production.size());
        file.write(reinterpret_cast<const char*>(&ap_count), sizeof(ap_count));
        for (const auto& entry : summary->accumulated_production) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }

        // Accumulated consumption.
        uint32_t ac_count = static_cast<uint32_t>(summary->accumulated_consumption.size());
        file.write(reinterpret_cast<const char*>(&ac_count), sizeof(ac_count));
        for (const auto& entry : summary->accumulated_consumption) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }
    }

    file.close();
    return file.good();
}

bool SaveManager::read_planet_data(const std::string& planet_dir,
                                    int64_t& out_seed,
                                    std::string& out_dimension_id,
                                    PlanetSummaryData& out_summary,
                                    bool& out_has_summary) {
    // Try planet_data.bin first (v2), fall back to planet_header.bin (v1).
    std::string path = planet_dir + "/planet_data.bin";
    bool is_legacy = false;

    if (!fs::exists(path)) {
        path = planet_dir + "/planet_header.bin";
        is_legacy = true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) {
        return false;
    }

    // Validate version.
    if (is_legacy) {
        if (version != kPlanetHeaderVersion_Legacy) {
            return false;
        }
    } else {
        if (version != kPlanetDataVersion) {
            return false;
        }
    }

    // Read seed.
    file.read(reinterpret_cast<char*>(&out_seed), sizeof(out_seed));
    if (!file.good()) {
        return false;
    }

    // Read dimension_id.
    if (!read_string(file, out_dimension_id)) {
        return false;
    }

    // Read summary section (v2 only).
    out_has_summary = false;
    if (!is_legacy) {
        uint8_t has_summary;
        file.read(reinterpret_cast<char*>(&has_summary), sizeof(has_summary));
        if (!file.good()) {
            return true;  // Header is valid, just no summary.
        }

        out_has_summary = (has_summary == 1);
        if (out_has_summary) {
            file.read(reinterpret_cast<char*>(&out_summary.captured_tick),
                      sizeof(out_summary.captured_tick));
            if (!file.good()) return false;

            // Production lines.
            uint32_t pl_count;
            file.read(reinterpret_cast<char*>(&pl_count), sizeof(pl_count));
            if (!file.good()) return false;
            out_summary.production_lines.resize(pl_count);
            for (uint32_t i = 0; i < pl_count; ++i) {
                if (!read_string(file, out_summary.production_lines[i].recipe_key)) return false;
                file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].rate_per_minute),
                          sizeof(float));
                if (!file.good()) return false;
                file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].active_count),
                          sizeof(int32_t));
                if (!file.good()) return false;
            }

            // Mining sites.
            uint32_t ms_count;
            file.read(reinterpret_cast<char*>(&ms_count), sizeof(ms_count));
            if (!file.good()) return false;
            out_summary.mining_sites.resize(ms_count);
            for (uint32_t i = 0; i < ms_count; ++i) {
                if (!read_string(file, out_summary.mining_sites[i].ore_key)) return false;
                file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].rate_per_minute),
                          sizeof(float));
                if (!file.good()) return false;
                file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].remaining_approx),
                          sizeof(int64_t));
                if (!file.good()) return false;
            }

            // Storage levels.
            uint32_t sl_count;
            file.read(reinterpret_cast<char*>(&sl_count), sizeof(sl_count));
            if (!file.good()) return false;
            out_summary.storage_levels.resize(sl_count);
            for (uint32_t i = 0; i < sl_count; ++i) {
                if (!read_string(file, out_summary.storage_levels[i].item_key)) return false;
                file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].count),
                          sizeof(int32_t));
                if (!file.good()) return false;
                file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].capacity),
                          sizeof(int32_t));
                if (!file.good()) return false;
            }

            // Power summary.
            file.read(reinterpret_cast<char*>(&out_summary.power_consumption_mw), sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.power_generation_mw), sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.power_surplus_mw), sizeof(float));
            if (!file.good()) return false;

            // Accumulated production.
            uint32_t ap_count;
            file.read(reinterpret_cast<char*>(&ap_count), sizeof(ap_count));
            if (!file.good()) return false;
            out_summary.accumulated_production.resize(ap_count);
            for (uint32_t i = 0; i < ap_count; ++i) {
                if (!read_string(file, out_summary.accumulated_production[i].item_key)) return false;
                file.read(reinterpret_cast<char*>(&out_summary.accumulated_production[i].amount),
                          sizeof(double));
                if (!file.good()) return false;
            }

            // Accumulated consumption.
            uint32_t ac_count;
            file.read(reinterpret_cast<char*>(&ac_count), sizeof(ac_count));
            if (!file.good()) return false;
            out_summary.accumulated_consumption.resize(ac_count);
            for (uint32_t i = 0; i < ac_count; ++i) {
                if (!read_string(file, out_summary.accumulated_consumption[i].item_key)) return false;
                file.read(reinterpret_cast<char*>(&out_summary.accumulated_consumption[i].amount),
                          sizeof(double));
                if (!file.good()) return false;
            }
        }
    }

    return true;
}

// --- Universe header ---

bool SaveManager::write_universe_header(const std::string& save_dir,
                                        int64_t seed,
                                        const std::string& universe_mode) {
    if (!ensure_directory(save_dir)) {
        return false;
    }

    std::string header_path = save_dir + "/universe_header.bin";
    std::ofstream file(header_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version = kUniverseHeaderVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, universe_mode);

    file.close();
    return file.good();
}

std::tuple<bool, int64_t, std::string> SaveManager::read_universe_header(
        const std::string& save_dir) {
    std::string header_path = save_dir + "/universe_header.bin";

    std::ifstream file(header_path, std::ios::binary);
    if (!file.is_open()) {
        return {false, 0, ""};
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good() || version != kUniverseHeaderVersion) {
        return {false, 0, ""};
    }

    int64_t seed;
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    if (!file.good()) {
        return {false, 0, ""};
    }

    std::string universe_mode;
    if (!read_string(file, universe_mode)) {
        return {false, 0, ""};
    }

    return {true, seed, universe_mode};
}

// --- Utility ---

std::vector<std::string> SaveManager::list_planets(
        const std::string& save_dir) {
    std::vector<std::string> result;

    std::string planets_dir = save_dir + "/planets";
    if (!fs::exists(planets_dir) || !fs::is_directory(planets_dir)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(planets_dir)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Accept both planet_data.bin (v2) and planet_header.bin (v1 legacy).
        std::string v2_path = entry.path().string() + "/planet_data.bin";
        std::string v1_path = entry.path().string() + "/planet_header.bin";
        if ((fs::exists(v2_path) && fs::is_regular_file(v2_path)) ||
            (fs::exists(v1_path) && fs::is_regular_file(v1_path))) {
            result.push_back(entry.path().filename().string());
        }
    }

    return result;
}

std::string SaveManager::planet_dir(const std::string& save_dir,
                                    const std::string& dimension_id) {
    return save_dir + "/planets/" + dimension_id;
}

} // namespace science_and_theology
