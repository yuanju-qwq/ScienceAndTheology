#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "save/save_manager.hpp"
#include "world/world_data.hpp"
#include "world_gen/terrain_generator.hpp"

namespace fs = std::filesystem;
using namespace science_and_theology;

namespace {

bool expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }
    std::cerr << "U0 world data smoke failed: " << message << '\n';
    return false;
}

bool verify_polar_surface_generation() {
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    auto add_material = [&](TerrainMaterialId id, const std::string& key,
                            uint32_t flags) {
        TerrainMaterialDef material;
        material.id = id;
        material.key = key;
        material.flags = flags;
        config->materials.push_back(material);
        config->material_ids_by_key[key] = id;
        config->material_keys_by_id[id] = key;
    };

    add_material(0, "snt:air", 0);
    add_material(1, "snt:stone", TF_SOLID | TF_MINEABLE);
    add_material(2, "snt:dirt", TF_WALKABLE | TF_MINEABLE);
    add_material(3, "snt:sand", TF_WALKABLE | TF_MINEABLE);
    add_material(4, "snt:water", TF_LIQUID);
    add_material(5, "snt:lava", TF_LIQUID);
    add_material(6, "snt:deepstone", TF_SOLID | TF_MINEABLE);
    add_material(7, "snt:core_barrier", TF_SOLID | TF_INDESTRUCTIBLE);
    add_material(103, "snt:snow", TF_WALKABLE | TF_MINEABLE);
    add_material(104, "snt:ice", TF_SOLID | TF_WALKABLE | TF_MINEABLE);

    config->roles.air = 0;
    config->roles.stone = 1;
    config->roles.dirt = 2;
    config->roles.sand = 3;
    config->roles.water = 4;
    config->roles.lava = 5;
    config->roles.deepstone = 6;
    config->roles.core_barrier = 7;

    PlanetConfig planet;
    planet.dimension_id = "polar_test";
    planet.planet_radius = 512.0f;
    planet.center_y = -512.0f;
    planet.terrain_height_scale = 16.0f;
    planet.sea_level_fraction = 0.3f;
    planet.atmosphere_type = ATMO_BREATHABLE;
    config->planet_configs.push_back(planet);

    TerrainGenerator generator(WorldSeed(20260619), config);
    const ChunkData chunk = generator.generate_chunk("polar_test", 0, 0, 0);
    int snow_count = 0;
    int ice_count = 0;
    for (const TerrainCell& cell : chunk.terrain.cells) {
        if (cell.material == 103) ++snow_count;
        if (cell.material == 104) ++ice_count;
    }

    return expect(snow_count > 0, "polar spawn generated no snow")
        && expect(ice_count > 0, "polar ocean generated no ice")
        && expect(chunk.terrain.cell_at(0, 6, 0).material == 103,
                  "north-pole landing surface is not snow");
}

} // namespace

int main() {
	if (!verify_polar_surface_generation()) {
		return 1;
	}
	std::cerr << "[U0CoreSmoke] prepare unicode save path" << std::endl;
    const std::string dimension = "u0_smoke";
    const fs::path save_root =
        fs::temp_directory_path() / fs::u8path(u8"ScienceAndTheology_u0_smoke");
    const auto encoded_save_root = save_root.u8string();
    const std::string save_root_utf8(
        reinterpret_cast<const char*>(encoded_save_root.data()),
        encoded_save_root.size());

    std::error_code ec;
    fs::remove_all(save_root, ec);
	std::cerr << "[U0CoreSmoke] build source chunk" << std::endl;

    WorldData source;
    ChunkData chunk;
    chunk.state = ChunkState::ACTIVE;
    chunk.terrain.resize(
        ChunkData::kChunkSize,
        ChunkData::kChunkSize,
        ChunkData::kChunkSize);
    chunk.terrain.set_cell(1, 2, 3, 17, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(31, 0, 31, 23, TF_SOLID | TF_WALKABLE);

    BlockEntityPlacement machine;
    machine.id.id = 42;
    machine.entity_type = BlockEntityType::MACHINE;
    machine.root_x = -31;
    machine.root_y = 2;
    machine.root_z = 67;
    machine.type_data_json = R"({"machine_type":"furnace","facing":4})";
    chunk.block_entities.push_back(machine);

    source.set_chunk(dimension, -1, 0, 2, std::move(chunk));
    if (!expect(source.chunk_count() == 1, "source chunk was not registered")) {
        return 1;
    }

    const int saved = SaveManager::save_dimension(
        save_root_utf8, 20260619, dimension, source);
	std::cerr << "[U0CoreSmoke] save returned " << saved << std::endl;
    if (!expect(saved == 1, "expected one saved chunk")) {
        fs::remove_all(save_root, ec);
        return 1;
    }

    WorldData restored;
    int legacy_skipped = 0;
    const int loaded = SaveManager::load_dimension(
        save_root_utf8, dimension, restored, &legacy_skipped);
	std::cerr << "[U0CoreSmoke] load returned " << loaded << std::endl;
    if (!expect(loaded == 1, "expected one loaded chunk") ||
        !expect(legacy_skipped == 0, "unexpected legacy region files")) {
        fs::remove_all(save_root, ec);
        return 1;
    }

    const ChunkData* loaded_chunk = restored.get_chunk(dimension, -1, 0, 2);
    if (!expect(loaded_chunk != nullptr, "negative-coordinate chunk key changed")) {
        fs::remove_all(save_root, ec);
        return 1;
    }

    const TerrainCell& placed = loaded_chunk->terrain.cell_at(1, 2, 3);
    if (!expect(placed.material == 17, "placed voxel material changed") ||
        !expect(placed.is_solid() && placed.is_mineable(),
                "placed voxel flags changed") ||
        !expect(loaded_chunk->block_entities.size() == 1,
                "block entity placement was not restored") ||
        !expect(loaded_chunk->block_entities[0].id.id == 42,
                "block entity id changed") ||
        !expect(loaded_chunk->block_entities[0].root_x == -31,
                "block entity world position changed")) {
        fs::remove_all(save_root, ec);
        return 1;
    }

    restored.remove_chunk(dimension, -1, 0, 2);
    if (!expect(restored.chunk_count() == 0, "chunk removal failed")) {
        fs::remove_all(save_root, ec);
        return 1;
    }

    fs::remove_all(save_root, ec);
    std::cout << "U0 world data smoke passed: chunk, voxel, block entity, and region save/load.\n";
    return 0;
}
