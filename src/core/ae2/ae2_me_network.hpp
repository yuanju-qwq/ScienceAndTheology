#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ae2_resource_id.hpp"
#include "ae2_storage_cell.hpp"
#include "common/resource_key.hpp"
#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// ============================================================
// MENode types
// ============================================================

using MENodeId = uint32_t;
inline constexpr MENodeId kInvalidMENodeId = 0xFFFFFFFF;

enum class MENodeType : uint8_t {
    Drive,       // Holds storage cells
    StorageBus,  // Bridges external inventory
    Interface,   // PatternProvider + machine I/O
    Terminal,    // View/search
    Cable        // Junction only
};

struct MENode {
    MENodeId id = kInvalidMENodeId;
    MENodeType type = MENodeType::Cable;
};

// ============================================================
// MENetwork — ME cable network
// ============================================================
//
// Mirrors AE2's ME Network topology (ME Controller + cables).
// Nodes connected via ME cables form connected components.
// Within a component, all IStorage objects (cells + external
// inventories) are pooled together for unified query/extract/insert.

class MENetwork {
public:
    MENetwork() = default;
    ~MENetwork() = default;

    // --- Node lifecycle ---

    MENodeId add_node(MENodeType type);
    bool remove_node(MENodeId id);
    MENode* get_node(MENodeId id);
    const MENode* get_node(MENodeId id) const;
    size_t node_count() const { return nodes_.size(); }

    // --- Edge lifecycle ---

    bool connect(MENodeId a, MENodeId b);
    bool disconnect(MENodeId a, MENodeId b);
    bool are_connected(MENodeId a, MENodeId b) const;

    // --- Storage attachment ---
    // Attach a storage to a node. The network takes ownership.
    // Only meaningful for Drive and StorageBus nodes.

    void attach_storage(MENodeId node_id, std::unique_ptr<IStorage> storage);
    IStorage* detach_storage(MENodeId node_id, size_t index);
    const std::vector<std::unique_ptr<IStorage>>* node_storages(MENodeId id) const;

    // --- Global query (within context node's component) ---

    // Total available of a resource across all storages in the component.
    int64_t check(const ResourceKey& key, MENodeId context_node) const;
    int64_t check_id(const ResourceId& key, MENodeId context_node) const;

    // Network-wide query (sums all storages across all components).
    int64_t check_global(const ResourceKey& key) const;
    int64_t check_global_id(const ResourceId& key) const;

    // Extract from any storage in the component.
    // Returns amount actually extracted.
    int64_t extract(const ResourceKey& key, int64_t amount,
                    MENodeId context_node);
    int64_t extract_id(const ResourceId& key, int64_t amount,
                       MENodeId context_node);

    // Network-wide extract (prefers storages in the same component as context).
    int64_t extract_global(const ResourceKey& key, int64_t amount,
                           MENodeId context_node);
    int64_t extract_global_id(const ResourceId& key, int64_t amount,
                              MENodeId context_node);

    // Insert into any storage in the component.
    // Returns amount NOT inserted (overflow).
    int64_t insert(const ResourceKey& key, int64_t amount,
                   MENodeId context_node);
    int64_t insert_id(const ResourceId& key, int64_t amount,
                      MENodeId context_node);

    // Network-wide insert (tries context component first, then others).
    int64_t insert_global(const ResourceKey& key, int64_t amount,
                          MENodeId context_node);
    int64_t insert_global_id(const ResourceId& key, int64_t amount,
                             MENodeId context_node);

    // --- Network topology ---

    std::vector<MENodeId> find_connected_nodes(MENodeId start) const;
    void rebuild_components();

private:
    struct Impl {
        std::vector<std::unique_ptr<IStorage>> storages;
        int component_id = -1;
    };

    MENodeId next_id_ = 1;
    std::unordered_map<MENodeId, MENode> nodes_;
    std::unordered_map<MENodeId, Impl> node_data_;
    std::vector<std::pair<MENodeId, MENodeId>> edges_;

    int component_of(MENodeId id) const;
    const Impl* node_impl(MENodeId id) const;
    Impl* node_impl(MENodeId id);
    std::vector<MENodeId> nodes_in_component(int comp_id) const;
};

} // namespace science_and_theology::gt
