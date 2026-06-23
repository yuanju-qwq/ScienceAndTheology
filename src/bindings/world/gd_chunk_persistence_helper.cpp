#include "gd_chunk_persistence_helper.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "gd_world_data.h"
#include "core/save/save_manager.hpp"
#include "core/save/region_compactor.hpp"

namespace science_and_theology {

namespace {

GDWorldData* as_world_data(godot::Resource* resource) {
    return godot::Object::cast_to<GDWorldData>(resource);
}

godot::Dictionary compaction_result_to_dict(const RegionCompactionResult& result) {
    godot::Dictionary dict;
    dict["ok"] = result.ok;
    dict["region_files_scanned"] = result.region_files_scanned;
    dict["region_files_compacted"] = result.region_files_compacted;
    dict["chunk_entries_before"] = result.chunk_entries_before;
    dict["chunk_entries_after"] = result.chunk_entries_after;
    dict["duplicate_entries_removed"] = result.duplicate_entries_removed;
    dict["failed_region_files"] = result.failed_region_files;
    return dict;
}

} // namespace

bool GDChunkPersistenceHelper::save_chunk(
        const godot::String& save_dir,
        godot::Resource* world_data,
        const godot::String& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const {
    GDWorldData* gd_world = as_world_data(world_data);
    if (gd_world == nullptr) {
        godot::UtilityFunctions::push_warning(
            "GDChunkPersistenceHelper: save_chunk requires GDWorldData.");
        return false;
    }

    const std::string save_dir_str = save_dir.utf8().get_data();
    const std::string dim_str = dimension_id.utf8().get_data();
    const std::string planet_dir = SaveManager::planet_dir(save_dir_str, dim_str);
    return SaveManager::save_chunk(
        planet_dir, gd_world->get_seed(), dim_str, *gd_world->get_world_ptr(),
        chunk_x, chunk_y, chunk_z);
}

bool GDChunkPersistenceHelper::load_chunk(
        const godot::String& save_dir,
        godot::Resource* world_data,
        const godot::String& dimension_id,
        int chunk_x, int chunk_y, int chunk_z,
        bool emit_ready) const {
    GDWorldData* gd_world = as_world_data(world_data);
    if (gd_world == nullptr) {
        godot::UtilityFunctions::push_warning(
            "GDChunkPersistenceHelper: load_chunk requires GDWorldData.");
        return false;
    }

    const std::string save_dir_str = save_dir.utf8().get_data();
    const std::string dim_str = dimension_id.utf8().get_data();
    const std::string planet_dir = SaveManager::planet_dir(save_dir_str, dim_str);
    const bool ok = SaveManager::load_chunk(
        planet_dir, dim_str, *gd_world->get_world_ptr(), chunk_x, chunk_y, chunk_z);
    if (ok && emit_ready) {
        gd_world->emit_signal("chunk_ready", dimension_id, chunk_x, chunk_y, chunk_z);
    }
    return ok;
}

bool GDChunkPersistenceHelper::delete_chunk_from_save(
        const godot::String& save_dir,
        const godot::String& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const {
    const std::string save_dir_str = save_dir.utf8().get_data();
    const std::string dim_str = dimension_id.utf8().get_data();
    const std::string planet_dir = SaveManager::planet_dir(save_dir_str, dim_str);
    return SaveManager::delete_chunk(planet_dir, dim_str, chunk_x, chunk_y, chunk_z);
}

godot::Dictionary GDChunkPersistenceHelper::compact_region(
        const godot::String& save_dir,
        const godot::String& dimension_id,
        int region_x, int region_y, int region_z) const {
    const std::string save_dir_str = save_dir.utf8().get_data();
    const std::string dim_str = dimension_id.utf8().get_data();
    const std::string planet_dir = SaveManager::planet_dir(save_dir_str, dim_str);
    return compaction_result_to_dict(RegionCompactor::compact_region(
        planet_dir, dim_str, region_x, region_y, region_z));
}

godot::Dictionary GDChunkPersistenceHelper::compact_dimension(
        const godot::String& save_dir,
        const godot::String& dimension_id) const {
    const std::string save_dir_str = save_dir.utf8().get_data();
    const std::string dim_str = dimension_id.utf8().get_data();
    const std::string planet_dir = SaveManager::planet_dir(save_dir_str, dim_str);
    return compaction_result_to_dict(RegionCompactor::compact_dimension(
        planet_dir, dim_str));
}

void GDChunkPersistenceHelper::_bind_methods() {
    using B = GDChunkPersistenceHelper;
    godot::ClassDB::bind_method(godot::D_METHOD(
        "save_chunk", "save_dir", "world_data", "dimension_id",
        "chunk_x", "chunk_y", "chunk_z"),
        &B::save_chunk);
    godot::ClassDB::bind_method(godot::D_METHOD(
        "load_chunk", "save_dir", "world_data", "dimension_id",
        "chunk_x", "chunk_y", "chunk_z", "emit_ready"),
        &B::load_chunk,
        DEFVAL(true));
    godot::ClassDB::bind_method(godot::D_METHOD(
        "delete_chunk_from_save", "save_dir", "dimension_id",
        "chunk_x", "chunk_y", "chunk_z"),
        &B::delete_chunk_from_save);
    godot::ClassDB::bind_method(godot::D_METHOD(
        "compact_region", "save_dir", "dimension_id",
        "region_x", "region_y", "region_z"),
        &B::compact_region);
    godot::ClassDB::bind_method(godot::D_METHOD(
        "compact_dimension", "save_dir", "dimension_id"),
        &B::compact_dimension);
}

} // namespace science_and_theology
