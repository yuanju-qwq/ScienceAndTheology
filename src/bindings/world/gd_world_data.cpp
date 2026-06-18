#include "gd_world_data.h"

#include <algorithm>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/world/chunk_data.hpp"

VARIANT_ENUM_CAST(science_and_theology::GDWorldData::ChunkStateConst)

namespace science_and_theology {

using namespace godot;

GDWorldData::GDWorldData()
    : seed_(0) {
    worldgen_config_ = make_empty_world_gen_config();
    rebuild_generator();
}

GDWorldData::~GDWorldData() {
    _drain_all_async_tasks();
}

int64_t GDWorldData::get_seed() const {
    return seed_;
}

void GDWorldData::set_seed(int64_t seed) {
    if (seed_ == seed) {
        return;
    }
    seed_ = seed;
    rebuild_generator();
}

void GDWorldData::set_worldgen_config(Resource* config) {
    auto* worldgen_config = Object::cast_to<GDWorldGenConfig>(config);
    if (config != nullptr && worldgen_config == nullptr) {
        UtilityFunctions::push_warning(
            "GDWorldData: worldgen_config must be a GDWorldGenConfig resource.");
        return;
    }

    if (worldgen_config != nullptr) {
        worldgen_config_resource_ = Ref<GDWorldGenConfig>(worldgen_config);
    } else {
        worldgen_config_resource_.unref();
    }
    worldgen_config_ = worldgen_config != nullptr
        ? worldgen_config->get_snapshot()
        : make_empty_world_gen_config();
    // Sync the snapshot to the underlying WorldData so that
    // simulation systems (BlockPhysicsSystem, etc.) can access it.
    world_.set_worldgen_config(worldgen_config_);
    rebuild_generator();
}

Resource* GDWorldData::get_worldgen_config() const {
    return worldgen_config_resource_.ptr();
}

int64_t GDWorldData::get_worldgen_content_hash() const {
    if (!worldgen_config_) {
        return 0;
    }
    return static_cast<int64_t>(worldgen_config_->content_hash);
}

std::shared_ptr<const WorldGenConfigSnapshot> GDWorldData::get_worldgen_snapshot() const {
    if (worldgen_config_) {
        return worldgen_config_;
    }
    return make_empty_world_gen_config();
}

Dictionary GDWorldData::terrain_drop_to_dict(const TerrainDropDef& drop) const {
    Dictionary d;
    d["item_key"] = String(drop.item_key.c_str());
    d["item_id"] = static_cast<int64_t>(drop.item_id);
    d["count"] = drop.count;
    d["min_count"] = drop.min_count;
    d["max_count"] = drop.max_count;
    d["chance"] = drop.chance;
    return d;
}

Dictionary GDWorldData::terrain_material_to_dict(const TerrainMaterialDef& def) const {
    Dictionary d;
    d["id"] = static_cast<int>(def.id);
    d["key"] = String(def.key.c_str());
    d["title_key"] = String(def.title_key.c_str());
    d["flags"] = static_cast<int64_t>(def.flags);
    d["hardness"] = def.hardness;
    d["required_tool_tag"] = String(def.required_tool_tag.c_str());
    d["required_mining_level"] = def.required_mining_level;

    Array drops;
    for (const auto& drop : def.drops) {
        drops.append(terrain_drop_to_dict(drop));
    }
    d["drops"] = drops;
    return d;
}

Dictionary GDWorldData::get_terrain_material_def(int64_t material_id) const {
    const auto snapshot = get_worldgen_snapshot();
    const auto* def = snapshot->find_material(
        static_cast<TerrainMaterialId>(material_id));
    if (def == nullptr) {
        return Dictionary();
    }
    return terrain_material_to_dict(*def);
}

void GDWorldData::rebuild_generator() {
    _drain_all_async_tasks();

    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        async_results_ = std::queue<AsyncChunkResult>();
        pending_async_chunks_.clear();
    }

    if (!worldgen_config_) {
        worldgen_config_ = make_empty_world_gen_config();
    }

    generator_ = std::make_unique<TerrainGenerator>(
        WorldSeed(static_cast<uint64_t>(seed_)), worldgen_config_);
}

godot::Dictionary GDWorldData::get_or_generate_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDWorldData: generator not initialized");
        return Dictionary();
    }

    std::string dimension_str = dimension_id.utf8().get_data();

    // Return existing chunk if already generated.
    if (world_.has_chunk(dimension_str, chunk_x, chunk_y, chunk_z)) {
        const ChunkData* existing = world_.get_chunk(
            dimension_str, chunk_x, chunk_y, chunk_z);
        if (existing != nullptr) {
            Dictionary result = terrain_to_dict(existing->terrain);
            result["connectors"] = connectors_to_array(existing->connectors);
            result["mechanisms"] = mechanisms_to_array(existing->mechanisms);
            result["entities"] = entity_ids_to_array(existing->entities);
            result["machines"] = entity_ids_to_array(existing->machines);
            result["connector_ids"] = entity_ids_to_array(existing->connector_ids);
            return result;
        }
    }

    // Generate new chunk.
    ChunkData chunk = generator_->generate_chunk(
        dimension_str, chunk_x, chunk_y, chunk_z);

    // Snapshot terrain and connectors before moving chunk into world.
    Dictionary result = terrain_to_dict(chunk.terrain);
    result["connectors"] = connectors_to_array(chunk.connectors);
    result["mechanisms"] = mechanisms_to_array(chunk.mechanisms);
    result["entities"] = entity_ids_to_array(chunk.entities);
    result["machines"] = entity_ids_to_array(chunk.machines);
    result["connector_ids"] = entity_ids_to_array(chunk.connector_ids);

    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        pending_async_chunks_.erase(
            AsyncChunkKey{dimension_str, chunk_x, chunk_y, chunk_z});
    }

    world_.set_chunk(dimension_str, chunk_x, chunk_y, chunk_z, std::move(chunk));

    // Notify Godot that a new chunk is ready for rendering.
    emit_signal("chunk_ready", dimension_id, chunk_x, chunk_y, chunk_z);

    return result;
}

int64_t GDWorldData::get_chunk_state(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return -1;
    }
    return static_cast<int64_t>(chunk->state);
}

void GDWorldData::set_chunk_state(
    const godot::String& dimension_id,
    int chunk_x, int chunk_y, int chunk_z, int state) {
    ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        UtilityFunctions::push_warning(
            "GDWorldData: cannot set state, chunk not found: ",
            dimension_id, " ", chunk_x, " ", chunk_y, " ", chunk_z);
        return;
    }
    chunk->state = static_cast<ChunkState>(state);
}

bool GDWorldData::has_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    return world_.has_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
}

void GDWorldData::set_chunk_from_dict(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    const godot::Dictionary& data) {
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunk.state = ChunkState::GENERATED;

    int size_x = static_cast<int>(data.get("size_x", 0));
    int size_y = static_cast<int>(data.get("size_y", 0));
    int size_z = static_cast<int>(data.get("size_z", 0));
    if (size_x <= 0 || size_y <= 0 || size_z <= 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: invalid chunk size in data dictionary");
        return;
    }

    chunk.terrain.resize(size_x, size_y, size_z);

    PackedByteArray materials = data.get("materials", PackedByteArray());
    PackedInt32Array flags = data.get("flags", PackedInt32Array());

    int cell_count = size_x * size_y * size_z;
    if (materials.size() < cell_count || flags.size() < cell_count) {
        UtilityFunctions::push_warning(
            "GDWorldData: materials/flags arrays too small for chunk dimensions");
        return;
    }

    for (int i = 0; i < cell_count; ++i) {
        chunk.terrain.cells[i].material =
            static_cast<TerrainMaterial>(materials[i]);
        chunk.terrain.cells[i].flags =
            static_cast<uint32_t>(flags[i]);
    }

    // Restore fluid data if present (backward compatible: missing = no fluid).
    PackedInt32Array fluid_types = data.get("fluid_types", PackedInt32Array());
    PackedInt32Array fluid_masses = data.get("fluid_masses", PackedInt32Array());
    PackedInt32Array fluid_temps = data.get("fluid_temps", PackedInt32Array());
    PackedByteArray fluid_gas_flags = data.get("fluid_gas_flags", PackedByteArray());

    if (fluid_types.size() >= cell_count &&
        fluid_masses.size() >= cell_count &&
        fluid_temps.size() >= cell_count) {
        for (int i = 0; i < cell_count; ++i) {
            chunk.terrain.cells[i].fluid_type =
                static_cast<CellFluidId>(fluid_types[i]);
            chunk.terrain.cells[i].fluid_mass =
                static_cast<int16_t>(fluid_masses[i]);
            chunk.terrain.cells[i].fluid_temperature =
                static_cast<int16_t>(fluid_temps[i]);
            if (fluid_gas_flags.size() > i) {
                chunk.terrain.cells[i].fluid_is_gas =
                    (fluid_gas_flags[i] != 0);
            }
        }
    }

    // Restore connectors from dict if present.
    Array connector_array = data.get("connectors", Array());
    for (int i = 0; i < connector_array.size(); ++i) {
        Dictionary conn_dict = connector_array[i];
        ConnectorPlacement conn;
        conn.connector_id = static_cast<int64_t>(
            conn_dict.get("connector_id", 0));
        conn.from_dimension = static_cast<std::string>(
            String(conn_dict.get("from_dimension", "")).utf8().get_data());
        conn.from_cell_x = static_cast<int>(conn_dict.get("from_cell_x", 0));
        conn.from_cell_y = static_cast<int>(conn_dict.get("from_cell_y", 0));
        conn.from_cell_z = static_cast<int>(conn_dict.get("from_cell_z", 0));
        conn.to_dimension = static_cast<std::string>(
            String(conn_dict.get("to_dimension", "")).utf8().get_data());
        conn.to_cell_x = static_cast<int>(conn_dict.get("to_cell_x", 0));
        conn.to_cell_y = static_cast<int>(conn_dict.get("to_cell_y", 0));
        conn.to_cell_z = static_cast<int>(conn_dict.get("to_cell_z", 0));
        conn.one_way = static_cast<bool>(conn_dict.get("one_way", false));
        conn.locked = static_cast<bool>(conn_dict.get("locked", false));
        conn.connector_type = static_cast<std::string>(
            String(conn_dict.get("connector_type", "")).utf8().get_data());
        conn.activation_mode = static_cast<int>(
            conn_dict.get("activation_mode", 0));
        chunk.connectors.push_back(std::move(conn));
    }

    // Restore mechanisms from dict if present.
    Array mechanism_array = data.get("mechanisms", Array());
    for (int i = 0; i < mechanism_array.size(); ++i) {
        Dictionary mechanism_dict = mechanism_array[i];
        MechanismPlacement mechanism;
        mechanism.mechanism_id = static_cast<std::string>(
            String(mechanism_dict.get("mechanism_id", "")).utf8().get_data());
        mechanism.dimension_id = static_cast<std::string>(
            String(mechanism_dict.get("dimension_id", "")).utf8().get_data());
        mechanism.cell_x = static_cast<int>(mechanism_dict.get("cell_x", 0));
        mechanism.cell_y = static_cast<int>(mechanism_dict.get("cell_y", 0));
        mechanism.cell_z = static_cast<int>(mechanism_dict.get("cell_z", 0));
        mechanism.title_key = static_cast<std::string>(
            String(mechanism_dict.get("title_key", "")).utf8().get_data());
        mechanism.action_label = static_cast<std::string>(
            String(mechanism_dict.get("action_label", "")).utf8().get_data());
        mechanism.flag_name = static_cast<std::string>(
            String(mechanism_dict.get("flag_name", "")).utf8().get_data());
        mechanism.activation_mode = static_cast<int>(
            mechanism_dict.get("activation_mode", 0));
        mechanism.one_shot = static_cast<bool>(
            mechanism_dict.get("one_shot", true));
        mechanism.required_flag = static_cast<std::string>(
            String(mechanism_dict.get("required_flag", "")).utf8().get_data());

        Array effect_array = mechanism_dict.get("effects", Array());
        for (int effect_index = 0; effect_index < effect_array.size();
             ++effect_index) {
            Dictionary effect_dict = effect_array[effect_index];
            MechanismEffectPlacement effect;
            effect.effect_type = static_cast<std::string>(
                String(effect_dict.get("type", "")).utf8().get_data());
            effect.connector_id = static_cast<int64_t>(
                effect_dict.get("connector_id", 0));
            effect.when_active = static_cast<bool>(
                effect_dict.get("when_active", false));
            effect.when_inactive = static_cast<bool>(
                effect_dict.get("when_inactive", true));
            mechanism.effects.push_back(std::move(effect));
        }

        chunk.mechanisms.push_back(std::move(mechanism));
    }

    // Restore entity IDs from dict if present.
    Array entity_array = data.get("entities", Array());
    chunk.entities = array_to_entity_ids(entity_array);

    // Restore machine IDs from dict if present.
    Array machine_array = data.get("machines", Array());
    chunk.machines = array_to_entity_ids(machine_array);

    // Restore connector runtime IDs from dict if present.
    Array conn_id_array = data.get("connector_ids", Array());
    chunk.connector_ids = array_to_entity_ids(conn_id_array);

    world_.set_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z, std::move(chunk));

    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        pending_async_chunks_.erase(AsyncChunkKey{
            dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z});
    }
}

godot::Dictionary GDWorldData::get_chunk_terrain(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Dictionary();
    }
    Dictionary result = terrain_to_dict(chunk->terrain);
    result["connectors"] = connectors_to_array(chunk->connectors);
    result["mechanisms"] = mechanisms_to_array(chunk->mechanisms);
    result["entities"] = entity_ids_to_array(chunk->entities);
    result["machines"] = entity_ids_to_array(chunk->machines);
    result["connector_ids"] = entity_ids_to_array(chunk->connector_ids);
    return result;
}

godot::Array GDWorldData::get_chunk_connectors(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Array();
    }
    return connectors_to_array(chunk->connectors);
}

godot::Array GDWorldData::get_chunk_mechanisms(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Array();
    }
    return mechanisms_to_array(chunk->mechanisms);
}

void GDWorldData::remove_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        pending_async_chunks_.erase(AsyncChunkKey{
            dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z});
    }
    world_.remove_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
}

godot::Array GDWorldData::get_all_chunk_keys() const {
    Array result;
    for (const auto& key : world_.all_chunk_keys()) {
        Dictionary key_dict;
        key_dict["dimension"] = String(key.dimension_id.c_str());
        key_dict["chunk_x"] = key.chunk_x;
        key_dict["chunk_y"] = key.chunk_y;
        key_dict["chunk_z"] = key.chunk_z;
        result.append(key_dict);
    }
    return result;
}

void GDWorldData::clear() {
    _drain_all_async_tasks();
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        async_results_ = std::queue<AsyncChunkResult>();
        pending_async_chunks_.clear();
    }
    world_.clear();
}

int64_t GDWorldData::get_chunk_count() const {
    return static_cast<int64_t>(world_.chunk_count());
}

// --- Gameplay config ---

godot::Dictionary GDWorldData::get_gameplay_config() const {
    const auto& gc = world_.gameplay_config();
    godot::Dictionary d;
    d["enable_collapse"] = gc.enable_collapse;
    d["collapse_chance_multiplier"] = gc.collapse_chance_multiplier;
    d["max_collapse_chain"] = gc.max_collapse_chain;
    d["support_beam_radius"] = gc.support_beam_radius;
    d["enable_gravity_fall"] = gc.enable_gravity_fall;
    d["max_gravity_fall_chain"] = gc.max_gravity_fall_chain;
    d["enable_day_night"] = gc.enable_day_night;
    d["day_length_seconds"] = gc.day_length_seconds;
    d["twilight_fraction"] = gc.twilight_fraction;
    d["days_per_season"] = gc.days_per_season;
    d["enable_season_colors"] = gc.enable_season_colors;

    // Serialize planet overrides.
    godot::Dictionary overrides;
    for (const auto& pair : gc.planet_overrides) {
        godot::Dictionary po;
        const auto& o = pair.second;
        po["has_enable_collapse"] = o.has_enable_collapse;
        po["enable_collapse"] = o.enable_collapse;
        po["has_collapse_chance_multiplier"] = o.has_collapse_chance_multiplier;
        po["collapse_chance_multiplier"] = o.collapse_chance_multiplier;
        po["has_max_collapse_chain"] = o.has_max_collapse_chain;
        po["max_collapse_chain"] = o.max_collapse_chain;
        po["has_support_beam_radius"] = o.has_support_beam_radius;
        po["support_beam_radius"] = o.support_beam_radius;
        po["has_enable_gravity_fall"] = o.has_enable_gravity_fall;
        po["enable_gravity_fall"] = o.enable_gravity_fall;
        po["has_max_gravity_fall_chain"] = o.has_max_gravity_fall_chain;
        po["max_gravity_fall_chain"] = o.max_gravity_fall_chain;
        po["has_enable_day_night"] = o.has_enable_day_night;
        po["enable_day_night"] = o.enable_day_night;
        po["has_day_length_seconds"] = o.has_day_length_seconds;
        po["day_length_seconds"] = o.day_length_seconds;
        po["has_twilight_fraction"] = o.has_twilight_fraction;
        po["twilight_fraction"] = o.twilight_fraction;
        overrides[godot::String(pair.first.c_str())] = po;
    }
    d["planet_overrides"] = overrides;
    return d;
}

void GDWorldData::set_gameplay_config(const godot::Dictionary& config) {
    GameplayConfig gc;
    gc.enable_collapse = config.get("enable_collapse", gc.enable_collapse);
    gc.collapse_chance_multiplier = static_cast<float>(
        config.get("collapse_chance_multiplier", gc.collapse_chance_multiplier));
    gc.max_collapse_chain = static_cast<int>(
        config.get("max_collapse_chain", gc.max_collapse_chain));
    gc.support_beam_radius = static_cast<int>(
        config.get("support_beam_radius", gc.support_beam_radius));
    gc.enable_gravity_fall = config.get("enable_gravity_fall", gc.enable_gravity_fall);
    gc.max_gravity_fall_chain = static_cast<int>(
        config.get("max_gravity_fall_chain", gc.max_gravity_fall_chain));
    gc.enable_day_night = config.get("enable_day_night", gc.enable_day_night);
    gc.day_length_seconds = static_cast<float>(
        config.get("day_length_seconds", gc.day_length_seconds));
    gc.twilight_fraction = static_cast<float>(
        config.get("twilight_fraction", gc.twilight_fraction));
    gc.days_per_season = static_cast<int>(
        config.get("days_per_season", gc.days_per_season));
    gc.enable_season_colors = config.get("enable_season_colors", gc.enable_season_colors);

    // Deserialize planet overrides.
    godot::Variant overrides_var = config.get("planet_overrides", godot::Dictionary());
    if (overrides_var.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary overrides = overrides_var;
        const godot::Array keys = overrides.keys();
        for (int i = 0; i < keys.size(); ++i) {
            godot::String dim_key = keys[i];
            godot::Variant po_var = overrides[dim_key];
            if (po_var.get_type() != godot::Variant::DICTIONARY) continue;
            godot::Dictionary po = po_var;

            GameplayConfig::PlanetOverride o;
            o.has_enable_collapse = po.get("has_enable_collapse", false);
            o.enable_collapse = po.get("enable_collapse", true);
            o.has_collapse_chance_multiplier = po.get("has_collapse_chance_multiplier", false);
            o.collapse_chance_multiplier = static_cast<float>(
                po.get("collapse_chance_multiplier", 1.0f));
            o.has_max_collapse_chain = po.get("has_max_collapse_chain", false);
            o.max_collapse_chain = static_cast<int>(po.get("max_collapse_chain", 64));
            o.has_support_beam_radius = po.get("has_support_beam_radius", false);
            o.support_beam_radius = static_cast<int>(po.get("support_beam_radius", 5));
            o.has_enable_gravity_fall = po.get("has_enable_gravity_fall", false);
            o.enable_gravity_fall = po.get("enable_gravity_fall", true);
            o.has_max_gravity_fall_chain = po.get("has_max_gravity_fall_chain", false);
            o.max_gravity_fall_chain = static_cast<int>(po.get("max_gravity_fall_chain", 64));

            o.has_enable_day_night = po.get("has_enable_day_night", false);
            o.enable_day_night = po.get("enable_day_night", true);
            o.has_day_length_seconds = po.get("has_day_length_seconds", false);
            o.day_length_seconds = static_cast<float>(
                po.get("day_length_seconds", 600.0f));
            o.has_twilight_fraction = po.get("has_twilight_fraction", false);
            o.twilight_fraction = static_cast<float>(
                po.get("twilight_fraction", 0.1f));

            gc.planet_overrides[dim_key.utf8().get_data()] = o;
        }
    }

    world_.set_gameplay_config(gc);
}

godot::Dictionary GDWorldData::get_gameplay_config_for_dimension(
    const godot::String& dimension_id) const {
    const auto& gc = world_.gameplay_config();
    std::string dim = dimension_id.utf8().get_data();
    godot::Dictionary d;
    d["enable_collapse"] = gc.is_collapse_enabled(dim);
    d["collapse_chance_multiplier"] = gc.get_collapse_chance_multiplier(dim);
    d["max_collapse_chain"] = gc.get_max_collapse_chain(dim);
    d["support_beam_radius"] = gc.get_support_beam_radius(dim);
    d["enable_gravity_fall"] = gc.is_gravity_fall_enabled(dim);
    d["max_gravity_fall_chain"] = gc.get_max_gravity_fall_chain(dim);
    d["enable_day_night"] = gc.is_day_night_enabled(dim);
    d["day_length_seconds"] = gc.get_day_length_seconds(dim);
    d["twilight_fraction"] = gc.get_twilight_fraction(dim);
    d["days_per_season"] = gc.days_per_season;
    d["enable_season_colors"] = gc.enable_season_colors;
    return d;
}

godot::Dictionary GDWorldData::terrain_to_dict(
    const TerrainData& terrain) const {
    Dictionary result;
    int cell_count = terrain.size_x * terrain.size_y * terrain.size_z;

    PackedByteArray materials;
    PackedInt32Array flags;
    PackedInt32Array fluid_types;
    PackedInt32Array fluid_masses;
    PackedInt32Array fluid_temps;
    PackedByteArray fluid_gas_flags;
    materials.resize(cell_count);
    flags.resize(cell_count);
    fluid_types.resize(cell_count);
    fluid_masses.resize(cell_count);
    fluid_temps.resize(cell_count);
    fluid_gas_flags.resize(cell_count);

    for (int i = 0; i < cell_count; ++i) {
        materials[i] = static_cast<uint8_t>(terrain.cells[i].material);
        flags[i] = static_cast<int32_t>(terrain.cells[i].flags);
        fluid_types[i] = static_cast<int32_t>(terrain.cells[i].fluid_type);
        fluid_masses[i] = static_cast<int32_t>(terrain.cells[i].fluid_mass);
        fluid_temps[i] = static_cast<int32_t>(terrain.cells[i].fluid_temperature);
        fluid_gas_flags[i] = terrain.cells[i].fluid_is_gas ? 1 : 0;
    }

    result["size_x"] = terrain.size_x;
    result["size_y"] = terrain.size_y;
    result["size_z"] = terrain.size_z;
    result["materials"] = materials;
    result["flags"] = flags;
    result["fluid_types"] = fluid_types;
    result["fluid_masses"] = fluid_masses;
    result["fluid_temps"] = fluid_temps;
    result["fluid_gas_flags"] = fluid_gas_flags;

    return result;
}

godot::Array GDWorldData::connectors_to_array(
    const std::vector<ConnectorPlacement>& connectors) const {
    Array arr;
    for (const auto& conn : connectors) {
        Dictionary d;
        d["connector_id"] = conn.connector_id;
        d["from_dimension"] = String(conn.from_dimension.c_str());
        d["from_cell_x"] = conn.from_cell_x;
        d["from_cell_y"] = conn.from_cell_y;
        d["from_cell_z"] = conn.from_cell_z;
        d["to_dimension"] = String(conn.to_dimension.c_str());
        d["to_cell_x"] = conn.to_cell_x;
        d["to_cell_y"] = conn.to_cell_y;
        d["to_cell_z"] = conn.to_cell_z;
        d["one_way"] = conn.one_way;
        d["locked"] = conn.locked;
        d["connector_type"] = String(conn.connector_type.c_str());
        d["activation_mode"] = conn.activation_mode;
        arr.append(d);
    }
    return arr;
}

godot::Array GDWorldData::mechanisms_to_array(
    const std::vector<MechanismPlacement>& mechanisms) const {
    Array arr;
    for (const auto& mechanism : mechanisms) {
        Dictionary d;
        d["mechanism_id"] = String(mechanism.mechanism_id.c_str());
        d["dimension_id"] = String(mechanism.dimension_id.c_str());
        d["cell_x"] = mechanism.cell_x;
        d["cell_y"] = mechanism.cell_y;
        d["cell_z"] = mechanism.cell_z;
        d["title_key"] = String(mechanism.title_key.c_str());
        d["action_label"] = String(mechanism.action_label.c_str());
        d["flag_name"] = String(mechanism.flag_name.c_str());
        d["activation_mode"] = mechanism.activation_mode;
        d["one_shot"] = mechanism.one_shot;
        d["required_flag"] = String(mechanism.required_flag.c_str());

        Array effects;
        for (const auto& effect : mechanism.effects) {
            Dictionary effect_dict;
            effect_dict["type"] = String(effect.effect_type.c_str());
            effect_dict["connector_id"] = effect.connector_id;
            effect_dict["when_active"] = effect.when_active;
            effect_dict["when_inactive"] = effect.when_inactive;
            effects.append(effect_dict);
        }
        d["effects"] = effects;

        arr.append(d);
    }
    return arr;
}

godot::Array GDWorldData::entity_ids_to_array(
    const std::vector<EntityId>& ids) const {
    Array arr;
    for (const auto& eid : ids) {
        arr.append(static_cast<int64_t>(eid.id));
    }
    return arr;
}

std::vector<EntityId> GDWorldData::array_to_entity_ids(
    const godot::Array& arr) {
    std::vector<EntityId> result;
    for (int i = 0; i < arr.size(); ++i) {
        EntityId eid;
        eid.id = static_cast<uint64_t>(static_cast<int64_t>(arr[i]));
        if (eid.is_valid()) {
            result.push_back(eid);
        }
    }
    return result;
}

godot::Array GDWorldData::get_chunk_entities(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->entities);
}

godot::Array GDWorldData::get_chunk_machines(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->machines);
}

godot::Array GDWorldData::get_chunk_connector_ids(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->connector_ids);
}

bool GDWorldData::add_entity_to_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    int64_t entity_id) {
    ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return false;
    }
    EntityId eid{static_cast<uint64_t>(entity_id)};
    if (!eid.is_valid()) {
        return false;
    }
    chunk->entities.push_back(eid);
    return true;
}

bool GDWorldData::add_machine_to_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    int64_t machine_id) {
    ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return false;
    }
    EntityId mid{static_cast<uint64_t>(machine_id)};
    if (!mid.is_valid()) {
        return false;
    }
    chunk->machines.push_back(mid);
    return true;
}

bool GDWorldData::add_connector_id_to_chunk(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    int64_t connector_id) {
    ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return false;
    }
    EntityId cid{static_cast<uint64_t>(connector_id)};
    if (!cid.is_valid()) {
        return false;
    }
    chunk->connector_ids.push_back(cid);
    return true;
}

godot::Dictionary GDWorldData::get_terrain_cell(
    const godot::String& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    int local_x, int local_y, int local_z) {
    godot::Dictionary d;
    const ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) return d;

    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return d;

    const auto& cell = chunk->terrain.cell_at(local_x, local_y, local_z);
    d["material"] = static_cast<int>(cell.material);
    d["flags"] = static_cast<int>(cell.flags);
    d["is_solid"] = cell.is_solid();
    d["is_mineable"] = cell.is_mineable();
    d["is_walkable"] = cell.is_walkable();
    d["is_liquid"] = cell.is_liquid();
    d["is_climbable"] = cell.is_climbable();
    d["is_indestructible"] = cell.is_indestructible();
    d["is_gravity_fall"] = cell.is_gravity_fall();
    d["is_collapse_risk"] = cell.is_collapse_risk();
    d["is_support_beam"] = cell.is_support_beam();
    // Fluid data
    d["has_fluid"] = cell.has_fluid();
    d["fluid_type"] = static_cast<int>(cell.fluid_type);
    d["fluid_mass"] = static_cast<int>(cell.fluid_mass);
    d["fluid_temperature"] = static_cast<int>(cell.fluid_temperature);
    d["fluid_is_gas"] = cell.fluid_is_gas;
    return d;
}

bool GDWorldData::set_terrain_cell(
    const godot::String& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    int local_x, int local_y, int local_z, int material) {
    ChunkData* chunk = world_.get_chunk(
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) return false;

    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return false;

    uint32_t flags = worldgen_config_
        ? worldgen_config_->flags_for_material(static_cast<TerrainMaterialId>(material))
        : 0;
    chunk->terrain.set_cell(
        local_x, local_y, local_z, static_cast<TerrainMaterial>(material), flags);
    return true;
}

void GDWorldData::request_chunk_async(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDWorldData: request_chunk_async called without generator");
        return;
    }

    std::string dimension_str = dimension_id.utf8().get_data();

    // Skip if chunk already exists.
    if (world_.has_chunk(dimension_str, chunk_x, chunk_y, chunk_z)) {
        emit_signal("chunk_ready", dimension_id, chunk_x, chunk_y, chunk_z);
        return;
    }

    AsyncChunkKey key{dimension_str, chunk_x, chunk_y, chunk_z};
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        if (pending_async_chunks_.find(key) != pending_async_chunks_.end()) {
            return;
        }

        // Reject if the async pipeline is at capacity.
        // Prevents unbounded queue growth when generation outpaces consumption.
        if (max_async_queue_size_ > 0 &&
            static_cast<int64_t>(pending_async_chunks_.size()) >= max_async_queue_size_) {
            return;
        }

        pending_async_chunks_.insert(key);
    }

    // TerrainGenerator::generate_chunk() is thread-safe:
    // - Only reads world_seed_ (const), all other state is stack-local.
    // - Each NoiseGenerator creates independent perm_ array from its sub-seed.
    // Safe to share across worker threads without locks.
    auto* td = new AsyncChunkTaskData{this, generator_.get(),
        std::move(dimension_str), chunk_x, chunk_y, chunk_z};
    WorkerThreadPool::TaskID task_id =
        WorkerThreadPool::get_singleton()->add_native_task(
            &GDWorldData::_async_chunk_callback, td, true, "ChunkGen");
    if (task_id != WorkerThreadPool::INVALID_TASK_ID) {
        active_native_tasks_.push_back(task_id);
    } else {
        {
            std::lock_guard<std::mutex> lock(async_results_mutex_);
            pending_async_chunks_.erase(key);
        }
        delete td;
    }
}

int64_t GDWorldData::process_async_results() {
    int64_t completed = 0;

    // Drain a bounded result batch under lock, then process outside lock.
    std::vector<AsyncChunkResult> batch;
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        int64_t budget = max_async_results_per_frame_;
        while (!async_results_.empty() &&
               (budget <= 0 || completed < budget)) {
            batch.push_back(std::move(async_results_.front()));
            async_results_.pop();
            ++completed;
        }
    }

    completed = 0;
    for (auto& result : batch) {
        godot::String gd_dimension(result.dimension_id.c_str());
        int cx = result.chunk_x;
        int cy = result.chunk_y;
        int cz = result.chunk_z;

        {
            std::lock_guard<std::mutex> lock(async_results_mutex_);
            auto pending_it = pending_async_chunks_.find(
                AsyncChunkKey{result.dimension_id, cx, cy, cz});
            if (pending_it == pending_async_chunks_.end()) {
                continue;
            }
            pending_async_chunks_.erase(pending_it);
        }

        if (world_.has_chunk(result.dimension_id, cx, cy, cz)) {
            continue;
        }

        // Store the generated chunk in the world.
        world_.set_chunk(result.dimension_id, cx, cy, cz, std::move(result.chunk));

        // Emit signal from the main thread (signal-safe).
        emit_signal("chunk_ready", gd_dimension, cx, cy, cz);
        ++completed;
    }

    async_completed_count_ += completed;

    // Clean up completed native task IDs.
    auto* pool = WorkerThreadPool::get_singleton();
    active_native_tasks_.erase(
        std::remove_if(active_native_tasks_.begin(),
                       active_native_tasks_.end(),
                       [pool](WorkerThreadPool::TaskID id) {
                           return pool->is_task_completed(id);
                       }),
        active_native_tasks_.end());

    return completed;
}

int64_t GDWorldData::get_async_pending_count() const {
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        return static_cast<int64_t>(pending_async_chunks_.size());
    }
}

int64_t GDWorldData::get_async_result_queue_size() const {
    std::lock_guard<std::mutex> lock(async_results_mutex_);
    return static_cast<int64_t>(async_results_.size());
}

bool GDWorldData::is_chunk_async_pending(
    const godot::String& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    std::lock_guard<std::mutex> lock(async_results_mutex_);
    return pending_async_chunks_.find(AsyncChunkKey{
        dimension_id.utf8().get_data(), chunk_x, chunk_y, chunk_z}) != pending_async_chunks_.end();
}

int64_t GDWorldData::get_worker_thread_count() const {
    return static_cast<int64_t>(OS::get_singleton()->get_processor_count());
}

int64_t GDWorldData::get_async_completed_count() const {
    return async_completed_count_;
}

int64_t GDWorldData::get_max_async_queue_size() const {
    return max_async_queue_size_;
}

void GDWorldData::set_max_async_queue_size(int64_t size) {
    max_async_queue_size_ = (size >= 0) ? size : 0;
}

int64_t GDWorldData::get_max_async_results_per_frame() const {
    return max_async_results_per_frame_;
}

void GDWorldData::set_max_async_results_per_frame(int64_t count) {
    max_async_results_per_frame_ = (count >= 0) ? count : 0;
}

void GDWorldData::_async_chunk_callback(void* userdata) {
    auto* td = static_cast<AsyncChunkTaskData*>(userdata);
    GDWorldData* self = td->world_data;

    ChunkData chunk = td->gen->generate_chunk(
        td->dimension_id, td->chunk_x, td->chunk_y, td->chunk_z);

    AsyncChunkResult result;
    result.dimension_id = std::move(td->dimension_id);
    result.chunk_x = td->chunk_x;
    result.chunk_y = td->chunk_y;
    result.chunk_z = td->chunk_z;
    result.chunk = std::move(chunk);

    {
        std::lock_guard<std::mutex> lock(self->async_results_mutex_);
        if (self->pending_async_chunks_.find(AsyncChunkKey{
                result.dimension_id, result.chunk_x, result.chunk_y, result.chunk_z}) !=
            self->pending_async_chunks_.end()) {
            self->async_results_.push(std::move(result));
        }
    }

    delete td;
}

void GDWorldData::_drain_all_async_tasks() {
    auto* pool = WorkerThreadPool::get_singleton();
    for (auto task_id : active_native_tasks_) {
        pool->wait_for_task_completion(task_id);
    }
    active_native_tasks_.clear();
}

godot::Array GDWorldData::list_saves(const godot::String& base_saves_dir) {
    std::string dir_str = base_saves_dir.utf8().get_data();
    auto saves = SaveManager::list_saves(dir_str);

    Array result;
    for (const auto& save_name : saves) {
        result.append(String(save_name.c_str()));
    }
    return result;
}

int64_t GDWorldData::save_dimension(const godot::String& save_dir,
                                     const godot::String& dimension_id) {
    std::string dir_str = save_dir.utf8().get_data();
    std::string dim_str = dimension_id.utf8().get_data();

    // Use the per-dimension save API which writes to a planet subdirectory.
    std::string pdir = SaveManager::planet_dir(dir_str, dim_str);
    int count = SaveManager::save_dimension(pdir, seed_, dim_str, world_);
    if (count < 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: save_dimension failed for dimension: ", dimension_id);
    }
    return static_cast<int64_t>(count);
}

int64_t GDWorldData::load_dimension(const godot::String& save_dir,
                                     const godot::String& dimension_id) {
    std::string dir_str = save_dir.utf8().get_data();
    std::string dim_str = dimension_id.utf8().get_data();

    // Use the per-dimension load API which reads from a planet subdirectory.
    std::string pdir = SaveManager::planet_dir(dir_str, dim_str);
    int count = SaveManager::load_dimension(pdir, dim_str, world_);
    if (count < 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: load_dimension failed for dimension: ", dimension_id);
        return -1;
    }

    return static_cast<int64_t>(count);
}

int64_t GDWorldData::unload_dimension(const godot::String& dimension_id) {
    std::string dim_str = dimension_id.utf8().get_data();
    int64_t removed = 0;

    // Collect all chunk keys for this dimension, then remove them.
    auto keys = world_.all_chunk_keys();
    for (const auto& key : keys) {
        if (key.dimension_id == dim_str) {
            world_.remove_chunk(key.dimension_id,
                               key.chunk_x, key.chunk_y, key.chunk_z);
            removed++;
        }
    }

    return removed;
}

int64_t GDWorldData::get_dimension_chunk_count(
        const godot::String& dimension_id) const {
    std::string dim_str = dimension_id.utf8().get_data();
    int64_t count = 0;

    for (const auto& key : world_.all_chunk_keys()) {
        if (key.dimension_id == dim_str) {
            count++;
        }
    }

    return count;
}

// --- Universe header ---

bool GDWorldData::write_universe_header(const godot::String& save_dir,
                                         int64_t seed,
                                         const godot::String& universe_mode) {
    std::string dir_str = save_dir.utf8().get_data();
    std::string mode_str = universe_mode.utf8().get_data();
    return SaveManager::write_universe_header(dir_str, seed, mode_str);
}

godot::Dictionary GDWorldData::read_universe_header(
        const godot::String& save_dir) {
    std::string dir_str = save_dir.utf8().get_data();
    auto [ok, seed, mode] = SaveManager::read_universe_header(dir_str);

    Dictionary result;
    result["ok"] = ok;
    result["seed"] = seed;
    result["universe_mode"] = String(mode.c_str());
    return result;
}

godot::Array GDWorldData::list_planets(const godot::String& save_dir) {
    std::string dir_str = save_dir.utf8().get_data();
    auto planets = SaveManager::list_planets(dir_str);

    Array result;
    for (const auto& dim : planets) {
        result.append(String(dim.c_str()));
    }
    return result;
}

// --- Planet data (header + summary binary) ---

// Helper: convert a PlanetSummaryData to a Dictionary for GDScript.
static godot::Dictionary summary_data_to_dict(const PlanetSummaryData& s) {
    using namespace godot;

    Dictionary d;
    d["captured_tick"] = s.captured_tick;

    // Production lines.
    Array pl;
    for (const auto& line : s.production_lines) {
        Dictionary entry;
        entry["recipe_key"] = String(line.recipe_key.c_str());
        entry["rate_per_minute"] = line.rate_per_minute;
        entry["active_count"] = line.active_count;
        pl.append(entry);
    }
    d["production_lines"] = pl;

    // Mining sites.
    Array ms;
    for (const auto& site : s.mining_sites) {
        Dictionary entry;
        entry["ore_key"] = String(site.ore_key.c_str());
        entry["rate_per_minute"] = site.rate_per_minute;
        entry["remaining_approx"] = site.remaining_approx;
        ms.append(entry);
    }
    d["mining_sites"] = ms;

    // Storage levels.
    Array sl;
    for (const auto& entry : s.storage_levels) {
        Dictionary e;
        e["item_key"] = String(entry.item_key.c_str());
        e["count"] = entry.count;
        e["capacity"] = entry.capacity;
        sl.append(e);
    }
    d["storage_levels"] = sl;

    // Power.
    Dictionary power;
    power["consumption_mw"] = s.power_consumption_mw;
    power["generation_mw"] = s.power_generation_mw;
    power["surplus_mw"] = s.power_surplus_mw;
    d["power_summary"] = power;

    // Accumulated production.
    Array ap;
    for (const auto& entry : s.accumulated_production) {
        Dictionary e;
        e["item_key"] = String(entry.item_key.c_str());
        e["amount"] = entry.amount;
        ap.append(e);
    }
    d["accumulated_production"] = ap;

    // Accumulated consumption.
    Array ac;
    for (const auto& entry : s.accumulated_consumption) {
        Dictionary e;
        e["item_key"] = String(entry.item_key.c_str());
        e["amount"] = entry.amount;
        ac.append(e);
    }
    d["accumulated_consumption"] = ac;

    return d;
}

// Helper: convert a Dictionary to a PlanetSummaryData for C++.
static PlanetSummaryData dict_to_summary_data(const godot::Dictionary& d) {
    PlanetSummaryData s;

    s.captured_tick = d.get("captured_tick", 0);

    // Production lines.
    Array pl = d.get("production_lines", Array());
    for (int i = 0; i < pl.size(); ++i) {
        Dictionary entry = pl[i];
        PlanetSummaryData::ProductionLine line;
        line.recipe_key = String(entry.get("recipe_key", "")).utf8().get_data();
        line.rate_per_minute = entry.get("rate_per_minute", 0.0);
        line.active_count = entry.get("active_count", 0);
        s.production_lines.push_back(std::move(line));
    }

    // Mining sites.
    Array ms = d.get("mining_sites", Array());
    for (int i = 0; i < ms.size(); ++i) {
        Dictionary entry = ms[i];
        PlanetSummaryData::MiningSite site;
        site.ore_key = String(entry.get("ore_key", "")).utf8().get_data();
        site.rate_per_minute = entry.get("rate_per_minute", 0.0);
        site.remaining_approx = entry.get("remaining_approx", 0);
        s.mining_sites.push_back(std::move(site));
    }

    // Storage levels.
    Array sl = d.get("storage_levels", Array());
    for (int i = 0; i < sl.size(); ++i) {
        Dictionary entry = sl[i];
        PlanetSummaryData::StorageEntry e;
        e.item_key = String(entry.get("item_key", "")).utf8().get_data();
        e.count = entry.get("count", 0);
        e.capacity = entry.get("capacity", 0);
        s.storage_levels.push_back(std::move(e));
    }

    // Power.
    Dictionary power = d.get("power_summary", Dictionary());
    s.power_consumption_mw = power.get("consumption_mw", 0.0);
    s.power_generation_mw = power.get("generation_mw", 0.0);
    s.power_surplus_mw = power.get("surplus_mw", 0.0);

    // Accumulated production.
    Array ap = d.get("accumulated_production", Array());
    for (int i = 0; i < ap.size(); ++i) {
        Dictionary entry = ap[i];
        PlanetSummaryData::AccumulatedEntry e;
        e.item_key = String(entry.get("item_key", "")).utf8().get_data();
        e.amount = entry.get("amount", 0.0);
        s.accumulated_production.push_back(std::move(e));
    }

    // Accumulated consumption.
    Array ac = d.get("accumulated_consumption", Array());
    for (int i = 0; i < ac.size(); ++i) {
        Dictionary entry = ac[i];
        PlanetSummaryData::AccumulatedEntry e;
        e.item_key = String(entry.get("item_key", "")).utf8().get_data();
        e.amount = entry.get("amount", 0.0);
        s.accumulated_consumption.push_back(std::move(e));
    }

    return s;
}

bool GDWorldData::write_planet_data(const godot::String& planet_dir,
                                     int64_t seed,
                                     const godot::String& dimension_id,
                                     const godot::Dictionary& summary_dict) {
    std::string pdir_str = planet_dir.utf8().get_data();
    std::string dim_str = dimension_id.utf8().get_data();

    PlanetSummaryData summary;
    const PlanetSummaryData* summary_ptr = nullptr;

    if (!summary_dict.is_empty()) {
        summary = dict_to_summary_data(summary_dict);
        summary_ptr = &summary;
    }

    return SaveManager::write_planet_data(pdir_str, seed, dim_str, summary_ptr);
}

godot::Dictionary GDWorldData::read_planet_data(const godot::String& planet_dir) {
    std::string pdir_str = planet_dir.utf8().get_data();

    int64_t seed = 0;
    std::string dimension_id;
    PlanetSummaryData summary;
    bool has_summary = false;

    bool ok = SaveManager::read_planet_data(pdir_str, seed, dimension_id,
                                             summary, has_summary);

    Dictionary result;
    result["ok"] = ok;
    if (ok) {
        result["seed"] = seed;
        result["dimension_id"] = String(dimension_id.c_str());
        result["has_summary"] = has_summary;
        result["summary"] = has_summary ? summary_data_to_dict(summary) : Dictionary();
    }
    return result;
}

void GDWorldData::_bind_methods() {
    // Seed property.
    ClassDB::bind_method(D_METHOD("get_seed"),
                         &GDWorldData::get_seed);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                         &GDWorldData::set_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"),
                 "set_seed", "get_seed");

    ClassDB::bind_method(D_METHOD("set_worldgen_config", "config"),
                         &GDWorldData::set_worldgen_config);
    ClassDB::bind_method(D_METHOD("get_worldgen_config"),
                         &GDWorldData::get_worldgen_config);
    ClassDB::bind_method(D_METHOD("get_worldgen_content_hash"),
                         &GDWorldData::get_worldgen_content_hash);
    ClassDB::bind_method(D_METHOD("get_terrain_material_def", "material_id"),
                         &GDWorldData::get_terrain_material_def);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "worldgen_config",
                 PROPERTY_HINT_RESOURCE_TYPE, "GDWorldGenConfig"),
                 "set_worldgen_config", "get_worldgen_config");

    // Chunk generation and query.
    ClassDB::bind_method(D_METHOD("get_or_generate_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_or_generate_chunk);
    ClassDB::bind_method(D_METHOD("get_chunk_state", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_state);
    ClassDB::bind_method(D_METHOD("set_chunk_state", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "state"),
                         &GDWorldData::set_chunk_state);
    ClassDB::bind_method(D_METHOD("has_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::has_chunk);
    ClassDB::bind_method(D_METHOD("set_chunk_from_dict", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "data"),
                         &GDWorldData::set_chunk_from_dict);
    ClassDB::bind_method(D_METHOD("get_chunk_terrain", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_terrain);
    ClassDB::bind_method(D_METHOD("get_chunk_connectors", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_connectors);
    ClassDB::bind_method(D_METHOD("get_chunk_mechanisms", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_mechanisms);
    ClassDB::bind_method(D_METHOD("get_chunk_entities", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_entities);
    ClassDB::bind_method(D_METHOD("get_chunk_machines", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_machines);
    ClassDB::bind_method(D_METHOD("get_chunk_connector_ids", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::get_chunk_connector_ids);
    ClassDB::bind_method(D_METHOD("add_entity_to_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "entity_id"),
                         &GDWorldData::add_entity_to_chunk);
    ClassDB::bind_method(D_METHOD("add_machine_to_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "machine_id"),
                         &GDWorldData::add_machine_to_chunk);
    ClassDB::bind_method(D_METHOD("add_connector_id_to_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "connector_id"),
                         &GDWorldData::add_connector_id_to_chunk);
    ClassDB::bind_method(D_METHOD("get_terrain_cell", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "local_x", "local_y", "local_z"),
                         &GDWorldData::get_terrain_cell);
    ClassDB::bind_method(D_METHOD("set_terrain_cell", "dimension_id", "chunk_x", "chunk_y", "chunk_z", "local_x", "local_y", "local_z", "material"),
                         &GDWorldData::set_terrain_cell);
    ClassDB::bind_method(D_METHOD("remove_chunk", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::remove_chunk);
    ClassDB::bind_method(D_METHOD("get_all_chunk_keys"),
                         &GDWorldData::get_all_chunk_keys);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDWorldData::clear);
    ClassDB::bind_method(D_METHOD("get_chunk_count"),
                         &GDWorldData::get_chunk_count);

    // Gameplay config.
    ClassDB::bind_method(D_METHOD("get_gameplay_config"),
                         &GDWorldData::get_gameplay_config);
    ClassDB::bind_method(D_METHOD("set_gameplay_config", "config"),
                         &GDWorldData::set_gameplay_config);
    ClassDB::bind_method(D_METHOD("get_gameplay_config_for_dimension", "dimension_id"),
                         &GDWorldData::get_gameplay_config_for_dimension);

    // Async chunk generation.
    ClassDB::bind_method(D_METHOD("request_chunk_async", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::request_chunk_async);
    ClassDB::bind_method(D_METHOD("process_async_results"),
                         &GDWorldData::process_async_results);
    ClassDB::bind_method(D_METHOD("get_async_pending_count"),
                         &GDWorldData::get_async_pending_count);
    ClassDB::bind_method(D_METHOD("get_async_result_queue_size"),
                         &GDWorldData::get_async_result_queue_size);
    ClassDB::bind_method(D_METHOD("is_chunk_async_pending", "dimension_id", "chunk_x", "chunk_y", "chunk_z"),
                         &GDWorldData::is_chunk_async_pending);
    ClassDB::bind_method(D_METHOD("get_worker_thread_count"),
                         &GDWorldData::get_worker_thread_count);
    ClassDB::bind_method(D_METHOD("get_async_completed_count"),
                         &GDWorldData::get_async_completed_count);
    ClassDB::bind_method(D_METHOD("get_max_async_queue_size"),
                         &GDWorldData::get_max_async_queue_size);
    ClassDB::bind_method(D_METHOD("set_max_async_queue_size", "size"),
                         &GDWorldData::set_max_async_queue_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_async_queue_size"),
                 "set_max_async_queue_size", "get_max_async_queue_size");
    ClassDB::bind_method(D_METHOD("get_max_async_results_per_frame"),
                         &GDWorldData::get_max_async_results_per_frame);
    ClassDB::bind_method(D_METHOD("set_max_async_results_per_frame", "count"),
                         &GDWorldData::set_max_async_results_per_frame);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_async_results_per_frame"),
                 "set_max_async_results_per_frame",
                 "get_max_async_results_per_frame");

    // Save / load.
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("list_saves", "base_saves_dir"),
        &GDWorldData::list_saves);

    // Per-dimension save / load / unload.
    ClassDB::bind_method(D_METHOD("save_dimension", "save_dir", "dimension_id"),
                         &GDWorldData::save_dimension);
    ClassDB::bind_method(D_METHOD("load_dimension", "save_dir", "dimension_id"),
                         &GDWorldData::load_dimension);
    ClassDB::bind_method(D_METHOD("unload_dimension", "dimension_id"),
                         &GDWorldData::unload_dimension);
    ClassDB::bind_method(D_METHOD("get_dimension_chunk_count", "dimension_id"),
                         &GDWorldData::get_dimension_chunk_count);

    // Universe header.
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("write_universe_header", "save_dir", "seed", "universe_mode"),
        &GDWorldData::write_universe_header);
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("read_universe_header", "save_dir"),
        &GDWorldData::read_universe_header);
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("list_planets", "save_dir"),
        &GDWorldData::list_planets);

    // Planet data (header + summary binary).
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("write_planet_data", "planet_dir", "seed", "dimension_id", "summary_dict"),
        &GDWorldData::write_planet_data);
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("read_planet_data", "planet_dir"),
        &GDWorldData::read_planet_data);

    // Signal: emitted when a new chunk has been generated and stored.
    ADD_SIGNAL(MethodInfo("chunk_ready",
        PropertyInfo(Variant::STRING, "dimension_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y"),
        PropertyInfo(Variant::INT, "chunk_z")));

    // Chunk state constants.
    BIND_ENUM_CONSTANT(STATE_UNLOADED);
    BIND_ENUM_CONSTANT(STATE_GENERATING);
    BIND_ENUM_CONSTANT(STATE_GENERATED);
    BIND_ENUM_CONSTANT(STATE_ACTIVE);
    BIND_ENUM_CONSTANT(STATE_SLEEPING);

}

} // namespace science_and_theology
