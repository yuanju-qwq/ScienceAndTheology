#include "save_manager.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace science_and_theology {

int SaveManager::save_world(const std::string& save_dir,
                            int64_t seed, const WorldData& world) {
    if (!ensure_directory(save_dir)) {
        return -1;
    }

    std::string chunks_dir = save_dir + "/chunks";
    if (!ensure_directory(chunks_dir)) {
        return -1;
    }

    // Write world header.
    if (!write_world_header(save_dir, seed)) {
        return -1;
    }

    // Write each chunk.
    int saved_count = 0;
    for (const auto& key : world.all_chunk_keys()) {
        const ChunkData* chunk = world.get_chunk(
            key.layer_id, key.chunk_x, key.chunk_y);
        if (chunk == nullptr) {
            continue;
        }

        std::vector<uint8_t> data = ChunkSerializer::serialize(
            key.layer_id, *chunk);

        std::string file_path = chunks_dir + "/" +
            chunk_file_name(key.layer_id, key.chunk_x, key.chunk_y);

        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return -1;
        }

        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));

        if (!file.good()) {
            return -1;
        }

        file.close();
        ++saved_count;
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

    std::string chunks_dir = save_dir + "/chunks";
    if (!fs::exists(chunks_dir) || !fs::is_directory(chunks_dir)) {
        return {true, seed};
    }

    // Load each chunk file.
    for (const auto& entry : fs::directory_iterator(chunks_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string file_path = entry.path().string();

        // Read file contents.
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            continue;
        }

        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file_size <= 0) {
            continue;
        }

        std::vector<uint8_t> data(static_cast<size_t>(file_size));
        file.read(reinterpret_cast<char*>(data.data()), file_size);

        if (!file.good() && !file.eof()) {
            continue;
        }

        file.close();

        // Deserialize.
        std::string layer_id;
        ChunkData chunk;
        if (!ChunkSerializer::deserialize(data, layer_id, chunk)) {
            continue;
        }

        world.set_chunk(layer_id, chunk.chunk_x, chunk.chunk_y,
                        std::move(chunk));
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

std::string SaveManager::chunk_file_name(const std::string& layer_id,
                                         int chunk_x, int chunk_y) {
    std::ostringstream oss;
    oss << layer_id << "~" << chunk_x << "~" << chunk_y << ".bin";
    return oss.str();
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