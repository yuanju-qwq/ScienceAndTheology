#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../world/world_data.hpp"
#include "region_file.hpp"

namespace science_and_theology {

// Manages save/load of an entire world to/from disk.
// Each save is a directory containing a universe_header.bin and a planets/ folder.
//
// Directory structure (multi-planet, per-dimension):
//   {save_dir}/
//     universe_header.bin
//     universe_meta.json
//     planets/
//       {dimension_id}/
//         planet_data.bin     ← header + production summary (binary)
//         regions/
//           {dim}~{rx}~{ry}~{rz}.region
//
// planet_data.bin format (v2):
//   Header section:
//     uint8   version (2)
//     int64   seed
//     uint32  dimension_id_length + chars
//   Summary section (only if has_summary == 1):
//     uint8   has_summary
//     int64   captured_tick
//     uint32  production_line_count
//       for each: [uint32 recipe_key_len + chars] [float rate] [int32 count]
//     uint32  mining_site_count
//       for each: [uint32 ore_key_len + chars] [float rate] [int64 remaining]
//     uint32  storage_count
//       for each: [uint32 item_key_len + chars] [int32 count] [int32 capacity]
//     float   power_consumption_mw
//     float   power_generation_mw
//     float   power_surplus_mw
//     uint32  accumulated_production_count
//       for each: [uint32 item_key_len + chars] [double amount]
//     uint32  accumulated_consumption_count
//       for each: [uint32 item_key_len + chars] [double amount]
//
// Each region file groups 32×32×32 chunks to reduce file system
// overhead and improve I/O locality during bulk loads.
//
// Usage (per-dimension):
//   SaveManager::save_dimension("saves/my_world/planets/earth", 12345, "earth", world);
//   int count = SaveManager::load_dimension("saves/my_world/planets/earth", "earth", world);

// Struct representing a planet's production summary for binary serialization.
struct PlanetSummaryData {
    int64_t captured_tick = 0;

    struct ProductionLine {
        std::string recipe_key;
        float rate_per_minute = 0.0f;
        int32_t active_count = 0;
    };
    std::vector<ProductionLine> production_lines;

    struct MiningSite {
        std::string ore_key;
        float rate_per_minute = 0.0f;
        int64_t remaining_approx = 0;
    };
    std::vector<MiningSite> mining_sites;

    struct StorageEntry {
        std::string item_key;
        int32_t count = 0;
        int32_t capacity = 0;
    };
    std::vector<StorageEntry> storage_levels;

    float power_consumption_mw = 0.0f;
    float power_generation_mw = 0.0f;
    float power_surplus_mw = 0.0f;

    struct AccumulatedEntry {
        std::string item_key;
        double amount = 0.0;
    };
    std::vector<AccumulatedEntry> accumulated_production;
    std::vector<AccumulatedEntry> accumulated_consumption;

    bool has_production() const {
        for (const auto& line : production_lines) {
            if (line.rate_per_minute > 0.0f && line.active_count > 0) return true;
        }
        for (const auto& site : mining_sites) {
            if (site.rate_per_minute > 0.0f && site.remaining_approx > 0) return true;
        }
        return false;
    }
};

class SaveManager {
public:
    // Current universe header format version.
    static constexpr uint8_t kUniverseHeaderVersion = 1;

    // Current planet data format version (v2 includes summary).
    static constexpr uint8_t kPlanetDataVersion = 2;

    // --- Per-dimension save / load ---

    // Saves only chunks belonging to a specific dimension to a planet
    // subdirectory. Also writes the planet_data.bin header.
    // Returns the number of chunks saved, or -1 on error.
    static int save_dimension(const std::string& planet_dir,
                              int64_t seed,
                              const std::string& dimension_id,
                              const WorldData& world);

    // Loads only chunks belonging to a specific dimension from a planet
    // subdirectory. Does NOT clear existing chunks in WorldData.
    // Returns the number of chunks loaded, or -1 on error.
    static int load_dimension(const std::string& planet_dir,
                              const std::string& dimension_id,
                              WorldData& world);

    // --- Planet data (header + summary) ---

    // Writes a planet_data.bin file with header and optional summary.
    static bool write_planet_data(const std::string& planet_dir,
                                  int64_t seed,
                                  const std::string& dimension_id,
                                  const PlanetSummaryData* summary);

    // Reads a planet_data.bin file. Returns header info and optional summary.
    static bool read_planet_data(const std::string& planet_dir,
                                 int64_t& out_seed,
                                 std::string& out_dimension_id,
                                 PlanetSummaryData& out_summary,
                                 bool& out_has_summary);

    // --- Universe header ---

    static bool write_universe_header(const std::string& save_dir,
                                      int64_t seed,
                                      const std::string& universe_mode);

    static std::tuple<bool, int64_t, std::string> read_universe_header(
        const std::string& save_dir);

    // --- Utility ---

    static std::vector<std::string> list_saves(
        const std::string& base_saves_dir);

    static std::vector<std::string> list_planets(
        const std::string& save_dir);

    static std::string planet_dir(const std::string& save_dir,
                                  const std::string& dimension_id);

private:
    static bool ensure_directory(const std::string& path);

    // Helper: write a length-prefixed string to a binary stream.
    static void write_string(std::ofstream& file, const std::string& str);

    // Helper: read a length-prefixed string from a binary stream.
    static bool read_string(std::ifstream& file, std::string& out, uint32_t max_len = 256);
};

} // namespace science_and_theology