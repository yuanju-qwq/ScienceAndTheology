#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "gd_chunk_view.h"

namespace science_and_theology {

/// Manages the lifecycle and rendering of chunk views.
///
/// Uses a three-tier model for factory-game chunk management:
///   1. Loaded (simulation): chunks kept in memory for machine/network simulation
///   2. Visible (rendering): chunks with active GDChunkView (TileMapLayer)
///   3. Force-loaded: specific chunks always kept loaded regardless of distance
///
/// The simulation radius (loaded_radius) should be >= view_radius.
/// Force-loaded chunks override both radii — they stay loaded and simulated
/// even when the player is far away.
///
/// Usage (GDScript):
///   var chunk_mgr = GDChunkManager.new()
///   chunk_mgr.world_data = gd_world_data
///   chunk_mgr.loaded_radius = 10    # simulation keep-alive
///   chunk_mgr.view_radius = 6       # visual rendering
///   gd_world_data.chunk_ready.connect(chunk_mgr.on_chunk_ready)
///   # Each frame:
///   chunk_mgr.set_player_chunk("surface", player_cx, player_cy)
///   chunk_mgr.refresh_chunks()
///   # Force-load a factory chunk:
///   chunk_mgr.force_load_chunk("surface", 5, 3)
class GDChunkManager : public godot::Node {
    GDCLASS(GDChunkManager, godot::Node)

public:
    GDChunkManager();
    ~GDChunkManager() override;

    // -- Configuration --

    /// The GDWorldData resource providing chunk data.
    void set_world_data(godot::Resource* world);
    godot::Resource* get_world_data() const;

    /// Radius (in chunks) for keeping chunks in memory/simulation.
    /// Chunks within this radius stay in WorldData even if not visible.
    /// Default: 10 (covers 21x21 = 441 chunks).
    void set_loaded_radius(int64_t radius);
    int64_t get_loaded_radius() const;

    /// Radius (in chunks) for visual rendering.
    /// Chunks within this radius get GDChunkView (TileMapLayer).
    /// Default: 6 (covers 13x13 = 169 chunks).
    void set_view_radius(int64_t radius);
    int64_t get_view_radius() const;

    /// Tile size in pixels (default: 32).
    void set_tile_size(int64_t size);
    int64_t get_tile_size() const;

    /// Node paths for the layer container nodes where chunk views are parented.
    void set_surface_container_path(const godot::NodePath& path);
    godot::NodePath get_surface_container_path() const;
    void set_underground_container_path(const godot::NodePath& path);
    godot::NodePath get_underground_container_path() const;

    /// If true, chunks outside loaded_radius are unloaded from WorldData.
    /// Default: false (keep all generated chunks in memory for simplicity).
    void set_unload_distant_chunks(bool enable);
    bool get_unload_distant_chunks() const;

    /// Maximum new async chunk load requests issued per refresh_chunks() call.
    /// Default: 12. Set to 0 for unlimited.
    void set_max_chunk_load_requests_per_frame(int64_t count);
    int64_t get_max_chunk_load_requests_per_frame() const;

    /// Maximum GDChunkView instances created per refresh_chunks() call.
    /// Default: 2. Set to 0 for unlimited.
    void set_max_chunk_views_per_frame(int64_t count);
    int64_t get_max_chunk_views_per_frame() const;

    /// Returns the number of visible chunks queued for view creation.
    int64_t get_pending_visible_chunk_count() const;

    // -- Player tracking --

    /// Updates the player's current chunk position.
    void set_player_chunk(const godot::String& layer, int cx, int cy);

    /// Returns the current player chunk as a Dictionary.
    godot::Dictionary get_player_chunk() const;

    // -- Force-loaded chunks (factory chunks with machines) --

    /// Marks a chunk as force-loaded. It stays in memory and simulation
    /// regardless of player distance, until force_unload_chunk is called.
    void force_load_chunk(const godot::String& layer, int cx, int cy);

    /// Removes force-loaded status. The chunk may still be kept loaded
    /// if it falls within loaded_radius.
    void force_unload_chunk(const godot::String& layer, int cx, int cy);

    /// Returns true if the chunk is force-loaded.
    bool is_chunk_force_loaded(const godot::String& layer, int cx, int cy) const;

    /// Returns all force-loaded chunk keys as an Array of Dictionaries.
    godot::Array get_force_loaded_chunks() const;

    // -- Chunk management --

    /// Called when GDWorldData emits chunk_ready.
    /// Creates or updates the GDChunkView for the given chunk.
    void on_chunk_ready(const godot::String& layer, int cx, int cy);

    /// Main refresh function. Should be called each frame after set_player_chunk().
    /// Handles all three tiers: loaded, visible, and force-loaded chunks.
    void refresh_chunks();

    /// Immediately ensures a chunk is loaded in WorldData (requests async if needed).
    void ensure_chunk_loaded(const godot::String& layer, int cx, int cy);

    /// Creates a GDChunkView for a chunk (visual rendering).
    void show_chunk(const godot::String& layer, int cx, int cy);

    /// Removes the GDChunkView for a chunk (hides visual).
    void hide_chunk(const godot::String& layer, int cx, int cy);

    /// Fully unloads a chunk: removes view + optionally removes from WorldData.
    void unload_chunk(const godot::String& layer, int cx, int cy);

    /// Unloads all chunks (views + WorldData if unload_distant_chunks is true).
    void unload_all_chunks();

    /// Returns an array of chunk key Dictionaries for chunks with active views.
    godot::Array get_visible_chunks() const;

    /// Returns an array for chunks kept in memory (loaded in WorldData).
    godot::Array get_loaded_chunks() const;

    /// Returns the number of currently visible chunk views.
    int64_t get_visible_chunk_count() const;

    /// Returns the number of chunks kept in WorldData memory.
    int64_t get_loaded_chunk_count() const;

protected:
    static void _bind_methods();

private:
    struct ChunkPos {
        std::string layer;
        int cx = 0;
        int cy = 0;

        bool operator==(const ChunkPos& o) const {
            return layer == o.layer && cx == o.cx && cy == o.cy;
        }
    };

    struct ChunkPosHash {
        size_t operator()(const ChunkPos& p) const {
            size_t h = std::hash<std::string>()(p.layer);
            h ^= std::hash<int>()(p.cx) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(p.cy) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    bool is_within_loaded_radius(const std::string& layer, int cx, int cy) const;
    bool is_within_view_radius(const std::string& layer, int cx, int cy) const;
    bool should_be_loaded(const ChunkPos& pos) const;
    bool should_be_visible(const ChunkPos& pos) const;
    int64_t chunk_priority_distance_sq(const ChunkPos& pos) const;

    std::string layer_to_container_str(const std::string& layer) const;
    godot::Node* get_layer_container(const std::string& layer) const;
    godot::Node* ensure_layer_container(const std::string& layer);
    void position_chunk_view(GDChunkView* view, int cx, int cy);
    void enqueue_chunk_view(const ChunkPos& pos);
    void remove_queued_chunk_view(const ChunkPos& pos);
    void prune_visible_chunk_queue(
        const std::unordered_set<ChunkPos, ChunkPosHash>& should_be_visible_set);
    int64_t process_visible_chunk_queue(int64_t budget);

    // World data reference.
    godot::Resource* world_data_ = nullptr;

    // Configuration.
    int64_t loaded_radius_ = 10;
    int64_t view_radius_ = 6;
    int64_t tile_size_ = 32;
    bool unload_distant_chunks_ = false;
    int64_t max_chunk_load_requests_per_frame_ = 12;
    int64_t max_chunk_views_per_frame_ = 2;
    godot::NodePath surface_container_path_;
    godot::NodePath underground_container_path_;

    // Player tracking.
    std::string player_layer_ = "surface";
    int player_cx_ = 0;
    int player_cy_ = 0;

    // Force-loaded chunks (factory installations, machines, etc.).
    std::unordered_set<ChunkPos, ChunkPosHash> force_loaded_chunks_;

    // Chunk views (visual): ChunkPos -> GDChunkView*.
    std::unordered_map<ChunkPos, GDChunkView*, ChunkPosHash> visible_views_;

    // Visible chunks waiting for a GDChunkView. This decouples async chunk
    // completion from expensive TileMapLayer creation.
    std::vector<ChunkPos> pending_visible_chunks_;
    std::unordered_set<ChunkPos, ChunkPosHash> queued_visible_chunks_;

    // All chunks we know to be loaded in WorldData (tracking set).
    std::unordered_set<ChunkPos, ChunkPosHash> tracked_loaded_chunks_;
};

} // namespace science_and_theology
