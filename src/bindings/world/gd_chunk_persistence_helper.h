#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// Thin Godot binding around SaveManager's single-chunk persistence API.
// It accepts a GDWorldData resource so callers do not need direct access to the
// internal C++ WorldData pointer.
class GDChunkPersistenceHelper : public godot::Node {
    GDCLASS(GDChunkPersistenceHelper, godot::Node)

public:
    GDChunkPersistenceHelper() = default;
    ~GDChunkPersistenceHelper() override = default;

    bool save_chunk(const godot::String& save_dir,
                    godot::Resource* world_data,
                    const godot::String& dimension_id,
                    int chunk_x, int chunk_y, int chunk_z) const;

    bool load_chunk(const godot::String& save_dir,
                    godot::Resource* world_data,
                    const godot::String& dimension_id,
                    int chunk_x, int chunk_y, int chunk_z,
                    bool emit_ready = true) const;

    bool delete_chunk_from_save(const godot::String& save_dir,
                                const godot::String& dimension_id,
                                int chunk_x, int chunk_y, int chunk_z) const;

    // Safe compaction: rewrites region files without deleting unique chunk data.
    // Duplicate local chunk entries, if any, are collapsed by keeping the last
    // entry for that local coordinate.
    godot::Dictionary compact_region(const godot::String& save_dir,
                                     const godot::String& dimension_id,
                                     int region_x, int region_y, int region_z) const;

    godot::Dictionary compact_dimension(const godot::String& save_dir,
                                        const godot::String& dimension_id) const;

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
