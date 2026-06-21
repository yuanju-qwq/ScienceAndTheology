#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "save/save_manager.hpp"
#include "simulation/day_night_def.hpp"
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

bool verify_default_daylight() {
    const GameplayConfig config;
    const float time_of_day = compute_time_of_day(
        0, 12000, config.day_start_time);
    const DayNightState state = compute_day_night_state(
        time_of_day, config.twilight_fraction);
    return expect(std::abs(time_of_day - 0.5f) < 0.0001f,
                  "new world does not start at noon")
        && expect(state.is_daytime && state.sun_light_energy > 2.0f,
                  "new world default lighting is not daytime")
        && expect(std::abs(compute_time_of_day(
                      6000, 12000, config.day_start_time)) < 0.0001f,
                  "day phase offset does not wrap to midnight");
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
    for (const TerrainCell& cell : chunk.terrain.cells) {
        if (cell.material == 103) ++snow_count;
    }

    bool ok = expect(snow_count > 0, "polar spawn generated no snow")
        && expect(chunk.terrain.cell_at(0, 6, 0).material == 103,
                  "north-pole landing surface is not snow")
        && expect(chunk.terrain.cell_at(16, 6, 16).material == 103,
                  "landing plateau is not level at its center")
        && expect(chunk.terrain.cell_at(31, 6, 0).material == 103,
                  "landing plateau does not reach its configured radius")
        && expect(chunk.terrain.cell_at(16, 7, 16).material == 0,
                  "landing plateau has terrain above its level surface");

    auto dry_config = std::make_shared<WorldGenConfigSnapshot>(*config);
    dry_config->planet_configs[0].dimension_id = "landform_test";
    dry_config->planet_configs[0].sea_level_fraction = 0.0f;
    dry_config->planet_configs[0].atmosphere_type = ATMO_NONE;
    TerrainGenerator landform_generator(WorldSeed(20260619), dry_config);
    std::array<bool, static_cast<size_t>(TerrainGenerator::LandformZone::COUNT)>
        found_zones{};
    constexpr float kPi = 3.14159265358979323846f;
    for (int latitude = -85; latitude <= 85; latitude += 5) {
        const float lat = static_cast<float>(latitude) * kPi / 180.0f;
        for (int longitude = 0; longitude < 360; longitude += 5) {
            const float lon = static_cast<float>(longitude) * kPi / 180.0f;
            const float cos_lat = std::cos(lat);
            const auto zone = landform_generator.landform_zone_at_direction(
                "landform_test",
                cos_lat * std::cos(lon), std::sin(lat), cos_lat * std::sin(lon));
            found_zones[static_cast<size_t>(zone)] = true;
        }
    }
    for (size_t zone = 0; zone < found_zones.size(); ++zone) {
        ok = expect(found_zones[zone],
                    "landform coverage is missing zone " + std::to_string(zone))
            && ok;
    }

    const ChunkData left_low = generator.generate_chunk("polar_test", 2, -1, 0);
    const ChunkData left_high = generator.generate_chunk("polar_test", 2, 0, 0);
    const ChunkData right_low = generator.generate_chunk("polar_test", 3, -1, 0);
    const ChunkData right_high = generator.generate_chunk("polar_test", 3, 0, 0);
    auto surface_height = [](const ChunkData& low, const ChunkData& high,
                             int local_x, int local_z) {
        for (int y = 31; y >= 0; --y) {
            const auto material = high.terrain.cell_at(local_x, y, local_z).material;
            if (material != 0 && material != 4) return y;
        }
        for (int y = 31; y >= 0; --y) {
            const auto material = low.terrain.cell_at(local_x, y, local_z).material;
            if (material != 0 && material != 4) return y - 32;
        }
        return -1000;
    };
    int max_boundary_step = 0;
    for (int z = 0; z < 32; ++z) {
        const int left_y = surface_height(left_low, left_high, 31, z);
        const int right_y = surface_height(right_low, right_high, 0, z);
        if (left_y > -1000 && right_y > -1000) {
            max_boundary_step = std::max(
                max_boundary_step, std::abs(left_y - right_y));
        }
    }
    ok = expect(max_boundary_step <= 6,
                "terrain is discontinuous at chunk boundary; max step="
                    + std::to_string(max_boundary_step)) && ok;
    return ok;
}

} // namespace

int main() {
	if (!verify_default_daylight()) {
		return 1;
	}
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
