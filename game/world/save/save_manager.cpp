#include "save_manager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>

#include "chunk_serializer.h"
#include "voxel/storage/region_file.h"

#define SNT_LOG_CHANNEL "save"
#include "core/log.h"

namespace fs = std::filesystem;

namespace snt::game {

using snt::voxel::VoxelRegionEntry;
using snt::voxel::VoxelRegionFile;

namespace {

fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string region_file_path(const std::string& planet_dir,
                             const std::string& dimension_id,
                             int region_x, int region_y, int region_z) {
    return planet_dir + "/regions/" + VoxelRegionFile::region_file_name(
        dimension_id, region_x, region_y, region_z);
}

bool ensure_planet_header_if_missing(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id) {
    const std::string path = planet_dir + "/planet_data.bin";
    if (fs::exists(utf8_path(path))) {
        return true;
    }
    return GameSaveManager::write_planet_data(planet_dir, seed, dimension_id, nullptr);
}

bool read_existing_region(const std::string& file_path,
                          const std::string& expected_dimension,
                          int expected_rx, int expected_ry, int expected_rz,
                          std::vector<VoxelRegionEntry>& entries) {
    entries.clear();
    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::string file_dimension_id;
    int file_rx = 0;
    int file_ry = 0;
    int file_rz = 0;
    if (!VoxelRegionFile::read(file_path, file_dimension_id,
                          file_rx, file_ry, file_rz, entries)) {
        return false;
    }
    return file_dimension_id == expected_dimension
        && file_rx == expected_rx
        && file_ry == expected_ry
        && file_rz == expected_rz;
}

GameChunk assemble_game_chunk(const VoxelChunk& voxel,
                              const GameChunkSidecar* sidecar) {
    GameChunk result;
    result.voxel_chunk() = voxel;
    if (sidecar) result.sidecar() = *sidecar;
    return result;
}

void publish_game_chunk(ChunkRegistry& voxel_chunks,
                        GameChunkSidecarRegistry& sidecars,
                        ChunkKey key,
                        GameChunk chunk) {
    VoxelChunk voxel = std::move(chunk.voxel_chunk());
    GameChunkSidecar sidecar = std::move(chunk.sidecar());
    voxel_chunks.set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z,
                           std::move(voxel));
    sidecars.set(std::move(key), std::move(sidecar));
}

} // namespace

std::vector<std::string> GameSaveManager::list_saves(
    const std::string& base_saves_dir) {
    std::vector<std::string> result;

    const fs::path saves_path = utf8_path(base_saves_dir);
    if (!fs::exists(saves_path) || !fs::is_directory(saves_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(saves_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string save_path = path_to_utf8(entry.path());
        std::string header_path = save_path + "/universe_header.bin";

        if (fs::exists(utf8_path(header_path)) &&
            fs::is_regular_file(utf8_path(header_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

bool GameSaveManager::ensure_directory(const std::string& path) {
    const fs::path directory = utf8_path(path);
    if (fs::exists(directory)) {
        return fs::is_directory(directory);
    }

    std::error_code ec;
    bool created = fs::create_directories(directory, ec);
    return created && !ec;
}

// --- Per-dimension save / load ---

int GameSaveManager::save_dimension(const std::string& planet_dir,
                                    int64_t seed,
                                    const std::string& dimension_id,
                                    const ChunkRegistry& voxel_chunks,
                                    const GameChunkSidecarRegistry& sidecars) {
    if (!ensure_directory(planet_dir)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return -1;
    }

    // Write planet_data.bin header (no summary during save_dimension).
    try {
        if (!write_planet_data(planet_dir, seed, dimension_id, nullptr)) {
            return -1;
        }
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while writing planet_data.bin");
    }

    // Group chunks for this dimension by region.
    std::map<std::string, std::vector<VoxelRegionEntry>> region_chunks;
    std::map<std::string, std::tuple<int, int, int, std::string>> region_meta;

    try {
        const GameChunkSerializer serializer;
        for (const auto& key : voxel_chunks.all_chunk_keys()) {
            if (key.dimension_id != dimension_id) {
                continue;
            }

            const VoxelChunk* chunk = voxel_chunks.get_chunk(
                key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
            if (chunk == nullptr) {
                continue;
            }

            int rx = VoxelRegionFile::to_region(key.chunk_x);
            int ry = VoxelRegionFile::to_region(key.chunk_y);
            int rz = VoxelRegionFile::to_region(key.chunk_z);

            std::string file_name = VoxelRegionFile::region_file_name(
                key.dimension_id, rx, ry, rz);

            std::vector<uint8_t> data;
            try {
                const GameChunk game_chunk = assemble_game_chunk(*chunk, sidecars.get(key));
                data = serializer.serialize(key.dimension_id, game_chunk);
            } catch (const std::exception& error) {
                std::ostringstream message;
                message << "chunk serialization failed at " << key.dimension_id
                        << " (" << key.chunk_x << "," << key.chunk_y << ","
                        << key.chunk_z << "): " << error.what();
                throw std::runtime_error(message.str());
            } catch (...) {
                std::ostringstream message;
                message << "chunk serialization raised an unknown exception at "
                        << key.dimension_id << " (" << key.chunk_x << ","
                        << key.chunk_y << "," << key.chunk_z << ")";
                throw std::runtime_error(message.str());
            }

            VoxelRegionEntry entry;
            entry.local_x = static_cast<uint8_t>(VoxelRegionFile::to_local(key.chunk_x));
            entry.local_y = static_cast<uint8_t>(VoxelRegionFile::to_local(key.chunk_y));
            entry.local_z = static_cast<uint8_t>(VoxelRegionFile::to_local(key.chunk_z));
            entry.payload = std::move(data);

            region_chunks[file_name].push_back(std::move(entry));
            region_meta[file_name] = std::make_tuple(rx, ry, rz, key.dimension_id);
        }
    } catch (const std::exception&) {
        throw;
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while grouping chunks into regions");
    }

    // Write each region file.
    int saved_count = 0;
    try {
        for (auto& [file_name, chunks] : region_chunks) {
            auto& [rx, ry, rz, dim] = region_meta[file_name];
            std::string file_path = regions_dir + "/" + file_name;

            if (!VoxelRegionFile::write(file_path, rx, ry, rz, dim, chunks)) {
                return -1;
            }

            saved_count += static_cast<int>(chunks.size());
        }
    } catch (const std::exception&) {
        throw;
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while writing region files");
    }

    return saved_count;
}

int GameSaveManager::load_dimension(const std::string& planet_dir,
                                    const std::string& dimension_id,
                                    ChunkRegistry& voxel_chunks,
                                    GameChunkSidecarRegistry& sidecars) {

    // Read planet_data.bin to validate.
    int64_t seed = 0;
    std::string dim_id;
    GamePlanetSummaryData summary;
    bool has_summary = false;
    if (!read_planet_data(planet_dir, seed, dim_id, summary, has_summary)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    const fs::path regions_path = utf8_path(regions_dir);
    if (!fs::exists(regions_path) || !fs::is_directory(regions_path)) {
        return 0;
    }

    // Load each region file in this planet's directory.
    int loaded_count = 0;
    int rejected_region_count = 0;
    int rejected_chunk_count = 0;
    const GameChunkSerializer serializer;
    for (const auto& entry : fs::directory_iterator(regions_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string file_path = path_to_utf8(entry.path());

        std::string file_dimension_id;
        int region_x, region_y, region_z;
        std::vector<VoxelRegionEntry> entries;

        if (!VoxelRegionFile::read(file_path, file_dimension_id,
                              region_x, region_y, region_z, entries)) {
            ++rejected_region_count;
            continue;
        }

        if (file_dimension_id != dimension_id) {
            continue;
        }

        for (auto& chunk_entry : entries) {
            int chunk_x = region_x * VoxelRegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_x);
            int chunk_y = region_y * VoxelRegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_y);
            int chunk_z = region_z * VoxelRegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_z);

            std::string unused_dimension_id;
            GameChunk chunk;
            if (!serializer.deserialize(chunk_entry.payload, unused_dimension_id, chunk)) {
                ++rejected_chunk_count;
                continue;
            }

            publish_game_chunk(voxel_chunks, sidecars,
                               ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z},
                               std::move(chunk));
            loaded_count++;
        }
    }

    if (rejected_region_count != 0 || rejected_chunk_count != 0) {
        SNT_LOG_WARN(
            "load_dimension '%s': rejected %d region file(s) and %d chunk blob(s); "
            "only region v%u and chunk v%u are accepted",
            dimension_id.c_str(), rejected_region_count, rejected_chunk_count,
            static_cast<unsigned>(VoxelRegionFile::kCurrentVersion),
            static_cast<unsigned>(GameChunkSerializer::kCurrentVersion));
    }

    return loaded_count;
}

// --- Per-chunk save / load ---

bool GameSaveManager::save_chunk(const std::string& planet_dir,
                                 int64_t seed,
                                 const std::string& dimension_id,
                                 const ChunkRegistry& voxel_chunks,
                                 const GameChunkSidecarRegistry& sidecars,
                                 int chunk_x, int chunk_y, int chunk_z) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }
    const std::string regions_dir = planet_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return false;
    }
    if (!ensure_planet_header_if_missing(planet_dir, seed, dimension_id)) {
        return false;
    }

    const VoxelChunk* chunk = voxel_chunks.get_chunk(
        dimension_id, chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return false;
    }

    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    std::vector<VoxelRegionEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    VoxelRegionEntry replacement;
    replacement.local_x = lx;
    replacement.local_y = ly;
    replacement.local_z = lz;
    const GameChunkSerializer serializer;
    const GameChunk game_chunk = assemble_game_chunk(
        *chunk, sidecars.get(ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z}));
    replacement.payload = serializer.serialize(dimension_id, game_chunk);

    bool replaced = false;
    for (auto& entry : entries) {
        if (entry.local_x == lx && entry.local_y == ly && entry.local_z == lz) {
            entry = std::move(replacement);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(std::move(replacement));
    }

    return VoxelRegionFile::write(file_path, rx, ry, rz, dimension_id, entries);
}

bool GameSaveManager::load_chunk(const std::string& planet_dir,
                                 const std::string& dimension_id,
                                 ChunkRegistry& voxel_chunks,
                                 GameChunkSidecarRegistry& sidecars,
                                 int chunk_x, int chunk_y, int chunk_z) {
    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    std::vector<VoxelRegionEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.local_x != lx || entry.local_y != ly || entry.local_z != lz) {
            continue;
        }
        std::string unused_dimension_id;
        GameChunk chunk;
        const GameChunkSerializer serializer;
        if (!serializer.deserialize(entry.payload, unused_dimension_id, chunk)) {
            return false;
        }
        publish_game_chunk(voxel_chunks, sidecars,
                           ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z},
                           std::move(chunk));
        return true;
    }

    return false;
}

bool GameSaveManager::delete_chunk(const std::string& planet_dir,
                               const std::string& dimension_id,
                               int chunk_x, int chunk_y, int chunk_z) {
    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::vector<VoxelRegionEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    const auto old_size = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [lx, ly, lz](const VoxelRegionEntry& entry) {
                return entry.local_x == lx && entry.local_y == ly && entry.local_z == lz;
            }),
        entries.end());

    if (entries.size() == old_size) {
        return true;
    }

    if (entries.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return !ec;
    }

    return VoxelRegionFile::write(file_path, rx, ry, rz, dimension_id, entries);
}

// --- Planet data (header + summary) ---

void GameSaveManager::write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) {
        file.write(str.data(), len);
    }
}

bool GameSaveManager::read_string(std::ifstream& file, std::string& out,
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

bool GameSaveManager::write_planet_data(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id,
                                     const GamePlanetSummaryData* summary) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }

    std::string path = planet_dir + "/planet_data.bin";
    std::ofstream file(utf8_path(path), std::ios::binary);
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

bool GameSaveManager::read_planet_data(const std::string& planet_dir,
                                    int64_t& out_seed,
                                    std::string& out_dimension_id,
                                    GamePlanetSummaryData& out_summary,
                                    bool& out_has_summary) {
    std::string path = planet_dir + "/planet_data.bin";

    std::ifstream file(utf8_path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) {
        return false;
    }

    if (version != kPlanetDataVersion) {
        return false;
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

    // Read summary section.
    out_has_summary = false;
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

    return true;
}

bool GameSaveManager::write_universe_header(const std::string& save_dir,
                                        int64_t seed,
                                        const std::string& universe_mode) {
    if (!ensure_directory(save_dir)) {
        return false;
    }

    std::string header_path = save_dir + "/universe_header.bin";
    std::ofstream file(utf8_path(header_path), std::ios::binary);
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

std::tuple<bool, int64_t, std::string> GameSaveManager::read_universe_header(
        const std::string& save_dir) {
    std::string header_path = save_dir + "/universe_header.bin";

    std::ifstream file(utf8_path(header_path), std::ios::binary);
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

std::vector<std::string> GameSaveManager::list_planets(
        const std::string& save_dir) {
    std::vector<std::string> result;

    std::string planets_dir = save_dir + "/planets";
    const fs::path planets_path = utf8_path(planets_dir);
    if (!fs::exists(planets_path) || !fs::is_directory(planets_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(planets_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Only accept planet_data.bin (v2 format).
        std::string data_path =
            path_to_utf8(entry.path()) + "/planet_data.bin";
        if (fs::exists(utf8_path(data_path)) &&
            fs::is_regular_file(utf8_path(data_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

std::string GameSaveManager::planet_dir(const std::string& save_dir,
                                    const std::string& dimension_id) {
    return save_dir + "/planets/" + dimension_id;
}

} // namespace science_and_theology
