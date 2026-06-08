#pragma once

#include <godot_cpp/classes/tile_map_layer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/world/chunk_data.hpp"
#include "godot_adapters/terrain_material_palette.hpp"

namespace science_and_theology {

/// Visual representation of a single chunk as a TileMapLayer.
///
/// Each GDChunkView is a TileMapLayer positioned at the chunk's world origin.
/// Cells are local (0..kChunkSize-1, 0..kChunkSize-1) and rendered from
/// TerrainData received via update_from_terrain().
///
/// Usage (GDScript):
///   var view = GDChunkView.new()
///   view.layer_id = "surface"
///   view.chunk_x = 0
///   view.chunk_y = 0
///   view.update_from_terrain(terrain_dict)
class GDChunkView : public godot::TileMapLayer {
    GDCLASS(GDChunkView, godot::TileMapLayer)

public:
    GDChunkView();
    ~GDChunkView() override;

    /// Layer identifier (e.g. "surface", "underground").
    void set_layer_id(const godot::String& id);
    godot::String get_layer_id() const;

    /// Chunk coordinates.
    void set_chunk_x(int x);
    int get_chunk_x() const;
    void set_chunk_y(int y);
    int get_chunk_y() const;

    /// Populates this TileMapLayer from a terrain data dictionary.
    /// The dictionary format matches GDWorldData.get_chunk_terrain() output:
    ///   { "size_x": int, "size_y": int,
    ///     "materials": PackedByteArray, "flags": PackedInt32Array }
    void update_from_terrain(const godot::Dictionary& terrain_data);

    /// Clears all cells in this chunk view.
    void clear_chunk();

    /// Returns the chunk key for identification.
    godot::Dictionary get_chunk_key() const;

protected:
    static void _bind_methods();

private:
    void apply_tile(int local_x, int local_y, uint8_t material, int variant_seed);

    godot::String layer_id_;
    int chunk_x_ = 0;
    int chunk_y_ = 0;

    TerrainMaterialPalette palette_;
};

} // namespace science_and_theology
