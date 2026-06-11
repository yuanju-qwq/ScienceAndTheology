#include "gd_world_data.h"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/world/chunk_data.hpp"

VARIANT_ENUM_CAST(science_and_theology::GDWorldData::ChunkStateConst)
VARIANT_ENUM_CAST(science_and_theology::GDWorldData::MaterialConst)

namespace science_and_theology {

using namespace godot;

GDWorldData::GDWorldData()
    : seed_(0) {
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

void GDWorldData::rebuild_generator() {
    _drain_all_async_tasks();

    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        async_results_ = std::queue<AsyncChunkResult>();
    }

    generator_ = std::make_unique<TerrainGenerator>(
        WorldSeed(static_cast<uint64_t>(seed_)));
}

godot::Dictionary GDWorldData::get_or_generate_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDWorldData: generator not initialized");
        return Dictionary();
    }

    std::string layer_str = layer_id.utf8().get_data();

    // Return existing chunk if already generated.
    if (world_.has_chunk(layer_str, chunk_x, chunk_y)) {
        const ChunkData* existing = world_.get_chunk(
            layer_str, chunk_x, chunk_y);
        if (existing != nullptr) {
            Dictionary result = terrain_to_dict(existing->terrain);
            result["connectors"] = connectors_to_array(existing->connectors);
            result["entities"] = entity_ids_to_array(existing->entities);
            result["machines"] = entity_ids_to_array(existing->machines);
            result["connector_ids"] = entity_ids_to_array(existing->connector_ids);
            return result;
        }
    }

    // Generate new chunk.
    ChunkData chunk = generator_->generate_chunk(layer_str, chunk_x, chunk_y);

    // Snapshot terrain and connectors before moving chunk into world.
    Dictionary result = terrain_to_dict(chunk.terrain);
    result["connectors"] = connectors_to_array(chunk.connectors);
    result["entities"] = entity_ids_to_array(chunk.entities);
    result["machines"] = entity_ids_to_array(chunk.machines);
    result["connector_ids"] = entity_ids_to_array(chunk.connector_ids);

    world_.set_chunk(layer_str, chunk_x, chunk_y, std::move(chunk));

    // Notify Godot that a new chunk is ready for rendering.
    emit_signal("chunk_ready", layer_id, chunk_x, chunk_y);

    return result;
}

int64_t GDWorldData::get_chunk_state(
    const godot::String& layer_id, int chunk_x, int chunk_y) const {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return -1;
    }
    return static_cast<int64_t>(chunk->state);
}

void GDWorldData::set_chunk_state(
    const godot::String& layer_id, int chunk_x, int chunk_y, int state) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        UtilityFunctions::push_warning(
            "GDWorldData: cannot set state, chunk not found: ",
            layer_id, " ", chunk_x, " ", chunk_y);
        return;
    }
    chunk->state = static_cast<ChunkState>(state);
}

bool GDWorldData::has_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    return world_.has_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y);
}

void GDWorldData::set_chunk_from_dict(
    const godot::String& layer_id, int chunk_x, int chunk_y,
    const godot::Dictionary& data) {
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.state = ChunkState::GENERATED;

    int size_x = static_cast<int>(data.get("size_x", 0));
    int size_y = static_cast<int>(data.get("size_y", 0));
    if (size_x <= 0 || size_y <= 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: invalid chunk size in data dictionary");
        return;
    }

    chunk.terrain.resize(size_x, size_y);

    PackedByteArray materials = data.get("materials", PackedByteArray());
    PackedInt32Array flags = data.get("flags", PackedInt32Array());

    int cell_count = size_x * size_y;
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

    // Restore connectors from dict if present.
    Array connector_array = data.get("connectors", Array());
    for (int i = 0; i < connector_array.size(); ++i) {
        Dictionary conn_dict = connector_array[i];
        ConnectorPlacement conn;
        conn.connector_id = static_cast<int64_t>(
            conn_dict.get("connector_id", 0));
        conn.from_layer = static_cast<std::string>(
            String(conn_dict.get("from_layer", "")).utf8().get_data());
        conn.from_cell_x = static_cast<int>(conn_dict.get("from_cell_x", 0));
        conn.from_cell_y = static_cast<int>(conn_dict.get("from_cell_y", 0));
        conn.to_layer = static_cast<std::string>(
            String(conn_dict.get("to_layer", "")).utf8().get_data());
        conn.to_cell_x = static_cast<int>(conn_dict.get("to_cell_x", 0));
        conn.to_cell_y = static_cast<int>(conn_dict.get("to_cell_y", 0));
        conn.one_way = static_cast<bool>(conn_dict.get("one_way", false));
        conn.locked = static_cast<bool>(conn_dict.get("locked", false));
        conn.connector_type = static_cast<std::string>(
            String(conn_dict.get("connector_type", "")).utf8().get_data());
        conn.activation_mode = static_cast<int>(
            conn_dict.get("activation_mode", 0));
        chunk.connectors.push_back(std::move(conn));
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

    world_.set_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y,
                     std::move(chunk));
}

godot::Dictionary GDWorldData::get_chunk_terrain(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Dictionary();
    }
    Dictionary result = terrain_to_dict(chunk->terrain);
    result["connectors"] = connectors_to_array(chunk->connectors);
    result["entities"] = entity_ids_to_array(chunk->entities);
    result["machines"] = entity_ids_to_array(chunk->machines);
    result["connector_ids"] = entity_ids_to_array(chunk->connector_ids);
    return result;
}

godot::Array GDWorldData::get_chunk_connectors(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Array();
    }
    return connectors_to_array(chunk->connectors);
}

void GDWorldData::remove_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    world_.remove_chunk(layer_id.utf8().get_data(), chunk_x, chunk_y);
}

godot::Array GDWorldData::get_all_chunk_keys() const {
    Array result;
    for (const auto& key : world_.all_chunk_keys()) {
        Dictionary key_dict;
        key_dict["layer"] = String(key.layer_id.c_str());
        key_dict["chunk_x"] = key.chunk_x;
        key_dict["chunk_y"] = key.chunk_y;
        result.append(key_dict);
    }
    return result;
}

void GDWorldData::clear() {
    world_.clear();
}

int64_t GDWorldData::get_chunk_count() const {
    return static_cast<int64_t>(world_.chunk_count());
}

godot::Dictionary GDWorldData::terrain_to_dict(
    const TerrainData& terrain) const {
    Dictionary result;
    int cell_count = terrain.size_x * terrain.size_y;

    PackedByteArray materials;
    PackedInt32Array flags;
    materials.resize(cell_count);
    flags.resize(cell_count);

    for (int i = 0; i < cell_count; ++i) {
        materials[i] = static_cast<uint8_t>(terrain.cells[i].material);
        flags[i] = static_cast<int32_t>(terrain.cells[i].flags);
    }

    result["size_x"] = terrain.size_x;
    result["size_y"] = terrain.size_y;
    result["materials"] = materials;
    result["flags"] = flags;

    return result;
}

godot::Array GDWorldData::connectors_to_array(
    const std::vector<ConnectorPlacement>& connectors) const {
    Array arr;
    for (const auto& conn : connectors) {
        Dictionary d;
        d["connector_id"] = conn.connector_id;
        d["from_layer"] = String(conn.from_layer.c_str());
        d["from_cell_x"] = conn.from_cell_x;
        d["from_cell_y"] = conn.from_cell_y;
        d["to_layer"] = String(conn.to_layer.c_str());
        d["to_cell_x"] = conn.to_cell_x;
        d["to_cell_y"] = conn.to_cell_y;
        d["one_way"] = conn.one_way;
        d["locked"] = conn.locked;
        d["connector_type"] = String(conn.connector_type.c_str());
        d["activation_mode"] = conn.activation_mode;
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
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->entities);
}

godot::Array GDWorldData::get_chunk_machines(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->machines);
}

godot::Array GDWorldData::get_chunk_connector_ids(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) {
        return Array();
    }
    return entity_ids_to_array(chunk->connector_ids);
}

bool GDWorldData::add_entity_to_chunk(
    const godot::String& layer_id, int chunk_x, int chunk_y,
    int64_t entity_id) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
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
    const godot::String& layer_id, int chunk_x, int chunk_y,
    int64_t machine_id) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
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
    const godot::String& layer_id, int chunk_x, int chunk_y,
    int64_t connector_id) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
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
    const godot::String& layer_id, int chunk_x, int chunk_y,
    int local_x, int local_y) {
    godot::Dictionary d;
    const ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) return d;

    if (!chunk->terrain.is_valid_cell(local_x, local_y)) return d;

    const auto& cell = chunk->terrain.cell_at(local_x, local_y);
    d["material"] = static_cast<int>(cell.material);
    d["flags"] = static_cast<int>(cell.flags);
    d["is_solid"] = cell.is_solid();
    d["is_mineable"] = cell.is_mineable();
    d["is_walkable"] = cell.is_walkable();
    d["is_liquid"] = cell.is_liquid();
    return d;
}

bool GDWorldData::set_terrain_cell(
    const godot::String& layer_id, int chunk_x, int chunk_y,
    int local_x, int local_y, int material) {
    ChunkData* chunk = world_.get_chunk(
        layer_id.utf8().get_data(), chunk_x, chunk_y);
    if (chunk == nullptr) return false;

    if (!chunk->terrain.is_valid_cell(local_x, local_y)) return false;

    auto mat = static_cast<TerrainMaterial>(material);
    int old_mat = static_cast<int>(chunk->terrain.cell_at(local_x, local_y).material);
    chunk->terrain.set_cell(local_x, local_y, mat);
    return true;
}

void GDWorldData::request_chunk_async(
    const godot::String& layer_id, int chunk_x, int chunk_y) {
    if (!generator_) {
        UtilityFunctions::push_warning(
            "GDWorldData: request_chunk_async called without generator");
        return;
    }

    std::string layer_str = layer_id.utf8().get_data();

    // Skip if chunk already exists.
    if (world_.has_chunk(layer_str, chunk_x, chunk_y)) {
        emit_signal("chunk_ready", layer_id, chunk_x, chunk_y);
        return;
    }

    // Reject if the async pipeline is at capacity.
    // Prevents unbounded queue growth when generation outpaces consumption.
    if (max_async_queue_size_ > 0 &&
        get_async_pending_count() >= max_async_queue_size_) {
        return;
    }

    // TerrainGenerator::generate_chunk() is thread-safe:
    // - Only reads world_seed_ (const), all other state is stack-local.
    // - Each NoiseGenerator creates independent perm_ array from its sub-seed.
    // Safe to share across worker threads without locks.
    auto* td = new AsyncChunkTaskData{this, generator_.get(),
        std::move(layer_str), chunk_x, chunk_y};
    WorkerThreadPool::TaskID task_id =
        WorkerThreadPool::get_singleton()->add_native_task(
            &GDWorldData::_async_chunk_callback, td, true, "ChunkGen");
    if (task_id != WorkerThreadPool::INVALID_TASK_ID) {
        active_native_tasks_.push_back(task_id);
    } else {
        delete td;
    }
}

int64_t GDWorldData::process_async_results() {
    int64_t completed = 0;

    // Drain results under lock, then process outside lock.
    std::vector<AsyncChunkResult> batch;
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        while (!async_results_.empty()) {
            batch.push_back(std::move(async_results_.front()));
            async_results_.pop();
        }
    }

    for (auto& result : batch) {
        godot::String gd_layer(result.layer_id.c_str());
        int cx = result.chunk_x;
        int cy = result.chunk_y;

        // Store the generated chunk in the world.
        world_.set_chunk(result.layer_id, cx, cy, std::move(result.chunk));

        // Emit signal from the main thread (signal-safe).
        emit_signal("chunk_ready", gd_layer, cx, cy);
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
    int64_t pool_pending = static_cast<int64_t>(active_native_tasks_.size());

    int64_t result_pending = 0;
    {
        std::lock_guard<std::mutex> lock(async_results_mutex_);
        result_pending = static_cast<int64_t>(async_results_.size());
    }

    return pool_pending + result_pending;
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

void GDWorldData::_async_chunk_callback(void* userdata) {
    auto* td = static_cast<AsyncChunkTaskData*>(userdata);
    GDWorldData* self = td->world_data;

    ChunkData chunk = td->gen->generate_chunk(td->layer_id, td->chunk_x, td->chunk_y);

    AsyncChunkResult result;
    result.layer_id = std::move(td->layer_id);
    result.chunk_x = td->chunk_x;
    result.chunk_y = td->chunk_y;
    result.chunk = std::move(chunk);

    {
        std::lock_guard<std::mutex> lock(self->async_results_mutex_);
        self->async_results_.push(std::move(result));
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

int64_t GDWorldData::save_world(const godot::String& save_dir) {
    std::string dir_str = save_dir.utf8().get_data();
    int count = SaveManager::save_world(dir_str, seed_, world_);
    if (count < 0) {
        UtilityFunctions::push_warning(
            "GDWorldData: save_world failed for path: ", save_dir);
    }
    return static_cast<int64_t>(count);
}

int64_t GDWorldData::load_world(const godot::String& save_dir) {
    std::string dir_str = save_dir.utf8().get_data();

    auto [ok, loaded_seed] = SaveManager::load_world(dir_str, world_);
    if (!ok) {
        UtilityFunctions::push_warning(
            "GDWorldData: load_world failed for path: ", save_dir);
        return -1;
    }

    // Update seed to match loaded world.
    seed_ = loaded_seed;
    rebuild_generator();

    return loaded_seed;
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

void GDWorldData::_bind_methods() {
    // Seed property.
    ClassDB::bind_method(D_METHOD("get_seed"),
                         &GDWorldData::get_seed);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                         &GDWorldData::set_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"),
                 "set_seed", "get_seed");

    // Chunk generation and query.
    ClassDB::bind_method(D_METHOD("get_or_generate_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_or_generate_chunk);
    ClassDB::bind_method(D_METHOD("get_chunk_state", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_state);
    ClassDB::bind_method(D_METHOD("set_chunk_state", "layer_id", "chunk_x", "chunk_y", "state"),
                         &GDWorldData::set_chunk_state);
    ClassDB::bind_method(D_METHOD("has_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::has_chunk);
    ClassDB::bind_method(D_METHOD("set_chunk_from_dict", "layer_id", "chunk_x", "chunk_y", "data"),
                         &GDWorldData::set_chunk_from_dict);
    ClassDB::bind_method(D_METHOD("get_chunk_terrain", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_terrain);
    ClassDB::bind_method(D_METHOD("get_chunk_connectors", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_connectors);
    ClassDB::bind_method(D_METHOD("get_chunk_entities", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_entities);
    ClassDB::bind_method(D_METHOD("get_chunk_machines", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_machines);
    ClassDB::bind_method(D_METHOD("get_chunk_connector_ids", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::get_chunk_connector_ids);
    ClassDB::bind_method(D_METHOD("add_entity_to_chunk", "layer_id", "chunk_x", "chunk_y", "entity_id"),
                         &GDWorldData::add_entity_to_chunk);
    ClassDB::bind_method(D_METHOD("add_machine_to_chunk", "layer_id", "chunk_x", "chunk_y", "machine_id"),
                         &GDWorldData::add_machine_to_chunk);
    ClassDB::bind_method(D_METHOD("add_connector_id_to_chunk", "layer_id", "chunk_x", "chunk_y", "connector_id"),
                         &GDWorldData::add_connector_id_to_chunk);
    ClassDB::bind_method(D_METHOD("get_terrain_cell", "layer_id", "chunk_x", "chunk_y", "local_x", "local_y"),
                         &GDWorldData::get_terrain_cell);
    ClassDB::bind_method(D_METHOD("set_terrain_cell", "layer_id", "chunk_x", "chunk_y", "local_x", "local_y", "material"),
                         &GDWorldData::set_terrain_cell);
    ClassDB::bind_method(D_METHOD("remove_chunk", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::remove_chunk);
    ClassDB::bind_method(D_METHOD("get_all_chunk_keys"),
                         &GDWorldData::get_all_chunk_keys);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDWorldData::clear);
    ClassDB::bind_method(D_METHOD("get_chunk_count"),
                         &GDWorldData::get_chunk_count);

    // Async chunk generation.
    ClassDB::bind_method(D_METHOD("request_chunk_async", "layer_id", "chunk_x", "chunk_y"),
                         &GDWorldData::request_chunk_async);
    ClassDB::bind_method(D_METHOD("process_async_results"),
                         &GDWorldData::process_async_results);
    ClassDB::bind_method(D_METHOD("get_async_pending_count"),
                         &GDWorldData::get_async_pending_count);
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

    // Save / load.
    ClassDB::bind_method(D_METHOD("save_world", "save_dir"),
                         &GDWorldData::save_world);
    ClassDB::bind_method(D_METHOD("load_world", "save_dir"),
                         &GDWorldData::load_world);
    ClassDB::bind_static_method("GDWorldData",
        D_METHOD("list_saves", "base_saves_dir"),
        &GDWorldData::list_saves);

    // Signal: emitted when a new chunk has been generated and stored.
    ADD_SIGNAL(MethodInfo("chunk_ready",
        PropertyInfo(Variant::STRING, "layer_id"),
        PropertyInfo(Variant::INT, "chunk_x"),
        PropertyInfo(Variant::INT, "chunk_y")));

    // Chunk state constants.
    BIND_ENUM_CONSTANT(STATE_UNLOADED);
    BIND_ENUM_CONSTANT(STATE_GENERATING);
    BIND_ENUM_CONSTANT(STATE_GENERATED);
    BIND_ENUM_CONSTANT(STATE_ACTIVE);
    BIND_ENUM_CONSTANT(STATE_SLEEPING);

    // Terrain material constants.
    BIND_ENUM_CONSTANT(MAT_AIR);
    BIND_ENUM_CONSTANT(MAT_STONE);
    BIND_ENUM_CONSTANT(MAT_DIRT);
    BIND_ENUM_CONSTANT(MAT_SAND);
    BIND_ENUM_CONSTANT(MAT_WATER);
    BIND_ENUM_CONSTANT(MAT_LAVA);
    BIND_ENUM_CONSTANT(MAT_ORE_IRON);
    BIND_ENUM_CONSTANT(MAT_ORE_COPPER);
    BIND_ENUM_CONSTANT(MAT_ORE_COAL);
}

} // namespace science_and_theology
