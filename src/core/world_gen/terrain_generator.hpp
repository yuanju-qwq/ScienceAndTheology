#pragma once

#include <string>

#include "../world/chunk_data.hpp"
#include "world_seed.hpp"
#include "noise_generator.hpp"

namespace science_and_theology {

// Generates chunk terrain data using a multi-pass pipeline.
// Each pass adds one layer of detail to the chunk.
//
// Pass order:
//   1. Base Terrain  - fundamental material layout
//   2. Biome         - biome-specific material adjustments
//   3. Ore           - resource vein placement
//   4. Structure     - large structural features
//   5. Object        - small interactable objects
//   6. Gameplay      - connectors, spawn points, etc.
//
// Usage:
//   TerrainGenerator gen(WorldSeed(12345));
//   ChunkData chunk = gen.generate_chunk("surface", 0, 0);
class TerrainGenerator {
public:
    explicit TerrainGenerator(WorldSeed seed);

    // Generates a complete chunk for the given layer and chunk coordinates.
    // Returns a fully populated ChunkData ready to be inserted into WorldData.
    ChunkData generate_chunk(const std::string& layer_id,
                             int chunk_x, int chunk_y);

private:
    // Pass 1: Fill the chunk with base terrain materials.
    void pass_base_terrain(const std::string& layer_id,
                           int chunk_x, int chunk_y,
                           TerrainData& terrain);

    // Pass 2: Apply biome overrides (temperature, humidity, etc.).
    void pass_biome(const std::string& layer_id,
                    int chunk_x, int chunk_y,
                    TerrainData& terrain);

    // Pass 3: Place ore veins in mineable cells.
    void pass_ore(const std::string& layer_id,
                  int chunk_x, int chunk_y,
                  TerrainData& terrain);

    // Pass 4: Place large structures (ruins, caves, etc.).
    void pass_structure(const std::string& layer_id,
                        int chunk_x, int chunk_y,
                        TerrainData& terrain);

    // Pass 5: Place small interactive objects (altars, chests, etc.).
    void pass_object(const std::string& layer_id,
                     int chunk_x, int chunk_y,
                     TerrainData& terrain);

    // Pass 6: Place gameplay elements (connectors, spawn points, etc.).
    void pass_gameplay(const std::string& layer_id,
                       int chunk_x, int chunk_y,
                       TerrainData& terrain);

    // Sets a cell's material and derives its flags automatically.
    void set_cell(TerrainData& terrain, int x, int y,
                  TerrainMaterial material);

    // Checks if a cell is solid stone (candidate for ore placement).
    bool is_stone_cell(const TerrainCell& cell) const;

    // Generates a noise value mapping a cell position to [0, 1].
    float cell_noise(const NoiseGenerator& noise, int local_x, int local_y,
                     int chunk_x, int chunk_y, float scale) const;

    WorldSeed world_seed_;
};

} // namespace science_and_theology