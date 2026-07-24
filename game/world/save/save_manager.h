#pragma once

#include "core/expected.h"
#include "game/player/player_state.h"
#include "game/quest/quest_progress.h"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "game/world/game_chunk.h"

namespace snt::game {

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
//     players/
//       player_{hex_player_id}.quest  ← per-player quest progress (binary)
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
// Each region file groups 32×32×32 chunks to reduce file system overhead and
// improve I/O locality while chunk tickets load terrain on demand.

// Struct representing a planet's production summary for binary serialization.
struct GamePlanetSummaryData {
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

class GameSaveManager {
public:
    // Current universe header format version.
    static constexpr uint8_t kUniverseHeaderVersion = 1;

    // Current planet data format version (v2 includes summary).
    static constexpr uint8_t kPlanetDataVersion = 2;

    // Current per-player quest progress format version. Player progress is a
    // universe-level value and deliberately does not live in a spatial region
    // or one dimension's planet_data.bin.
    static constexpr uint8_t kQuestProgressVersion = 2;

    // Current per-player authoritative state format. Position, fixed-slot
    // inventory, equipment, bed respawn point, and opaque organ state are
    // universe-level values, never region payloads or client UI state.
    static constexpr uint8_t kPlayerStateVersion = 3;

    // --- Per-dimension save / load ---

    // Saves only chunks belonging to a specific dimension to a planet
    // subdirectory. Also writes the planet_data.bin header.
    // Returns the number of chunks saved, or -1 on error.
    static int save_dimension(const std::string& planet_dir,
                              int64_t seed,
                              const std::string& dimension_id,
                              const ChunkRegistry& voxel_chunks,
                              const GameChunkSidecarRegistry& sidecars);

    // Reads the durable sidecar for every persisted chunk without retaining
    // terrain in ChunkRegistry. The implementation validates each complete
    // current-format chunk payload before discarding its terrain portion, so
    // offline systems can safely own far-away machines and network islands.
    [[nodiscard]] static snt::core::Expected<size_t> load_dimension_sidecar_index(
        const std::string& planet_dir,
        const std::string& dimension_id,
        GameChunkSidecarRegistry& sidecars);

    // --- Per-player quest progress ---

    // Reads/writes one strict current-format quest file below a caller-owned
    // universe save root. Missing progress is a valid new-player result;
    // corrupt, mismatched, and trailing data are rejected instead of being
    // silently ignored. The write path keeps a recoverable previous file while
    // replacing the primary file.
    [[nodiscard]] static snt::core::Expected<std::vector<QuestProgressRecord>>
    load_quest_progress(const std::string& save_dir, std::string_view player_id);
    [[nodiscard]] static snt::core::Expected<void> save_quest_progress(
        const std::string& save_dir, std::string_view player_id,
        std::span<const QuestProgressRecord> progress);

    // Missing player state is a valid first-login result. Primary corruption,
    // format mismatches, and trailing data are rejected; backup is considered
    // only if the primary is absent.
    [[nodiscard]] static snt::core::Expected<std::optional<GamePlayerPersistentState>>
    load_player_state(const std::string& save_dir, std::string_view player_id);
    [[nodiscard]] static snt::core::Expected<void> save_player_state(
        const std::string& save_dir, std::string_view player_id,
        const GamePlayerPersistentState& state);

    // --- Per-chunk save / load ---

    // Saves terrain plus the current sidecar for one terrain-resident chunk.
    // Region entries outside this chunk remain untouched.
    [[nodiscard]] static snt::core::Expected<void> save_loaded_chunk(
        const std::string& planet_dir,
        int64_t seed,
        const std::string& dimension_id,
        const ChunkRegistry& voxel_chunks,
        const GameChunkSidecarRegistry& sidecars,
        int chunk_x, int chunk_y, int chunk_z);

    // Restores only terrain for one persisted chunk. It deliberately preserves
    // the caller's sidecar registry, whose values may have advanced through
    // offline simulation after the initial sidecar-index load. `false` means
    // the region or chunk entry is absent; malformed persisted data is an error.
    [[nodiscard]] static snt::core::Expected<bool> load_chunk_terrain(
        const std::string& planet_dir,
        const std::string& dimension_id,
        ChunkRegistry& voxel_chunks,
        int chunk_x, int chunk_y, int chunk_z);

    // Rewrites only one persisted chunk's semantic sidecar while preserving
    // its terrain payload. This is used for offline machine/island state after
    // the terrain itself has been dematerialized.
    [[nodiscard]] static snt::core::Expected<void> save_chunk_sidecar(
        const std::string& planet_dir,
        int64_t seed,
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z,
        const GameChunkSidecar& sidecar);

    // Removes one chunk entry from a region file. If the region becomes empty,
    // deletes the region file. This is the first safe region-level GC primitive.
    static bool delete_chunk(const std::string& planet_dir,
                             const std::string& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z);

    // --- Planet data (header + summary) ---

    // Writes a planet_data.bin file with header and optional summary.
    static bool write_planet_data(const std::string& planet_dir,
                                  int64_t seed,
                                  const std::string& dimension_id,
                                  const GamePlanetSummaryData* summary);

    // Reads a planet_data.bin file. Returns header info and optional summary.
    static bool read_planet_data(const std::string& planet_dir,
                                 int64_t& out_seed,
                                 std::string& out_dimension_id,
                                 GamePlanetSummaryData& out_summary,
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

}  // namespace snt::game
