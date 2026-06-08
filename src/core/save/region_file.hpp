#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace science_and_theology {

struct RegionChunkEntry {
    uint8_t local_x;
    uint8_t local_y;
    std::vector<uint8_t> data;
};

// Reads and writes Region files that group 32×32 chunks into a single file.
//
// Format (v1):
//   Header:
//     uint8   version (1)
//     int32   region_x
//     int32   region_y
//     string  layer_id (uint32 len + chars)
//   Index:
//     uint32  stored_count
//     For each stored chunk:
//       uint8   local_x (0..31)
//       uint8   local_y (0..31)
//       uint32  data_size (bytes)
//   Data:
//     For each stored chunk (in index order):
//       uint8[data_size]  serialized ChunkData blob
//
// Thread-safe: all methods are stateless and reentrant.
class RegionFile {
public:
    static constexpr int kRegionSize = 32;
    static constexpr uint8_t kVersion = 1;

    static bool write(const std::string& file_path,
                      int region_x, int region_y,
                      const std::string& layer_id,
                      const std::vector<RegionChunkEntry>& chunks);

    static bool read(const std::string& file_path,
                     std::string& out_layer_id,
                     int& out_region_x, int& out_region_y,
                     std::vector<RegionChunkEntry>& out_chunks);

    static inline int to_region(int chunk_coord) {
        if (chunk_coord >= 0) return chunk_coord / kRegionSize;
        return (chunk_coord + 1) / kRegionSize - 1;
    }

    static inline int to_local(int chunk_coord) {
        int r = to_region(chunk_coord);
        int local = chunk_coord - r * kRegionSize;
        if (local < 0) local += kRegionSize;
        return local;
    }

    static std::string region_file_name(const std::string& layer_id,
                                        int region_x, int region_y);
};

} // namespace science_and_theology
