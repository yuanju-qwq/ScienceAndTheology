#include "region_file.hpp"

#include <fstream>
#include <sstream>

namespace science_and_theology {

static void write_uint8(std::ofstream& file, uint8_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_int32(std::ofstream& file, int32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_uint32(std::ofstream& file, uint32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_uint32(file, len);
    if (len > 0) {
        file.write(str.data(), len);
    }
}

static bool read_uint8(std::ifstream& file, uint8_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_int32(std::ifstream& file, int32_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_uint32(std::ifstream& file, uint32_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_string(std::ifstream& file, std::string& out) {
    uint32_t len;
    if (!read_uint32(file, len)) return false;
    if (len == 0) {
        out.clear();
        return true;
    }
    out.resize(len);
    file.read(&out[0], len);
    return file.good();
}

bool RegionFile::write(const std::string& file_path,
                       int region_x, int region_y,
                       const std::string& layer_id,
                       const std::vector<RegionChunkEntry>& chunks) {
    if (chunks.size() > static_cast<size_t>(kRegionSize * kRegionSize))
        return false;

    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;

    write_uint8(file, kVersion);
    write_int32(file, region_x);
    write_int32(file, region_y);
    write_string(file, layer_id);

    write_uint32(file, static_cast<uint32_t>(chunks.size()));

    for (const auto& chunk : chunks) {
        write_uint8(file, chunk.local_x);
        write_uint8(file, chunk.local_y);
        write_uint32(file, static_cast<uint32_t>(chunk.data.size()));
    }

    for (const auto& chunk : chunks) {
        file.write(reinterpret_cast<const char*>(chunk.data.data()),
                   chunk.data.size());
    }

    file.close();
    return file.good();
}

bool RegionFile::read(const std::string& file_path,
                      std::string& out_layer_id,
                      int& out_region_x, int& out_region_y,
                      std::vector<RegionChunkEntry>& out_chunks) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;

    uint8_t version;
    if (!read_uint8(file, version)) return false;
    if (version != kVersion) return false;

    int32_t rx, ry;
    if (!read_int32(file, rx)) return false;
    if (!read_int32(file, ry)) return false;
    out_region_x = rx;
    out_region_y = ry;

    if (!read_string(file, out_layer_id)) return false;

    uint32_t count;
    if (!read_uint32(file, count)) return false;
    if (count > static_cast<uint32_t>(kRegionSize * kRegionSize)) return false;

    struct IndexEntry {
        uint8_t local_x;
        uint8_t local_y;
        uint32_t data_size;
    };
    std::vector<IndexEntry> index;
    index.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        IndexEntry entry;
        if (!read_uint8(file, entry.local_x)) return false;
        if (!read_uint8(file, entry.local_y)) return false;
        if (!read_uint32(file, entry.data_size)) return false;
        if (entry.local_x >= kRegionSize || entry.local_y >= kRegionSize)
            return false;
        index.push_back(entry);
    }

    out_chunks.clear();
    out_chunks.reserve(count);

    for (const auto& entry : index) {
        RegionChunkEntry chunk_entry;
        chunk_entry.local_x = entry.local_x;
        chunk_entry.local_y = entry.local_y;
        chunk_entry.data.resize(entry.data_size);
        file.read(reinterpret_cast<char*>(chunk_entry.data.data()),
                  entry.data_size);
        if (!file.good()) return false;
        out_chunks.push_back(std::move(chunk_entry));
    }

    return true;
}

std::string RegionFile::region_file_name(const std::string& layer_id,
                                         int region_x, int region_y) {
    std::ostringstream oss;
    oss << layer_id << "~" << region_x << "~" << region_y << ".region";
    return oss.str();
}

} // namespace science_and_theology
