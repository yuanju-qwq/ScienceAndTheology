#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/resource_key.hpp"
#include "../common/resource_types.hpp"
#include "ae2_resource_id.hpp"
#include "ae2_storage_cell.hpp"

namespace science_and_theology::gt {

// ============================================================
// MENode types
// ============================================================

using MENodeId = uint32_t;
inline constexpr MENodeId kInvalidMENodeId = 0xFFFFFFFF;

enum class MENodeType : uint8_t {
    Controller,  // Master controller — at most one per network, provides 32 channels
    Switch,      // Channel extender — adds channels to the network
    Drive,       // Holds storage cells
    StorageBus,  // Bridges external inventory
    Interface,   // PatternProvider + machine I/O
    Terminal,    // View/search
    Cable        // Junction only
};

struct MENode {
    MENodeId id = kInvalidMENodeId;
    MENodeType type = MENodeType::Cable;
    bool online = true;
};

// Additional state for Controller and Switch nodes.
struct ChannelProviderState {
    int channels = 32;
};

// ============================================================
// MENetwork — ME cable network
// ============================================================
//
// Tracks network topology via nodes + edges. Connected components represent
// separate ME networks. Each component pools all IStorage objects for unified
// check / extract / insert.
//
// Channel system:
//   - A network must have exactly one Controller (default 32 channels).
//   - Switches add extra channels (32 each).
//   - Each device (Drive, StorageBus, Interface, Terminal) consumes 1 channel.
//   - Cable nodes consume no channels.
//   - Devices beyond channel capacity are marked offline.

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

    void attach_storage(MENodeId node_id, std::unique_ptr<IStorage> storage);
    IStorage* detach_storage(MENodeId node_id, size_t index);
    const std::vector<std::unique_ptr<IStorage>>* node_storages(MENodeId id) const;

    // --- Query (component-scoped) ---

    int64_t check(const ResourceKey& key, MENodeId context_node) const;
    int64_t check_id(const ResourceId& key, MENodeId context_node) const;

    int64_t check_global(const ResourceKey& key) const;
    int64_t check_global_id(const ResourceId& key) const;

    int64_t extract(const ResourceKey& key, int64_t amount,
                    MENodeId context_node);
    int64_t extract_id(const ResourceId& key, int64_t amount,
                       MENodeId context_node);

    int64_t extract_global(const ResourceKey& key, int64_t amount,
                           MENodeId context_node);
    int64_t extract_global_id(const ResourceId& key, int64_t amount,
                              MENodeId context_node);

    int64_t insert(const ResourceKey& key, int64_t amount,
                   MENodeId context_node);
    int64_t insert_id(const ResourceId& key, int64_t amount,
                      MENodeId context_node);

    int64_t insert_global(const ResourceKey& key, int64_t amount,
                          MENodeId context_node);
    int64_t insert_global_id(const ResourceId& key, int64_t amount,
                             MENodeId context_node);

    // --- Network topology ---

    std::vector<MENodeId> find_connected_nodes(MENodeId start) const;
    void rebuild_components();

    // --- Channel system ---

    void set_channel_count(MENodeId id, int channels);
    int network_total_channels(MENodeId id) const;
    int network_online_devices(MENodeId id) const;
    bool is_node_online(MENodeId id) const;

private:
    struct Impl {
        std::vector<std::unique_ptr<IStorage>> storages;
        int component_id = -1;
    };

    struct ComponentInfo {
        bool has_controller = false;
        int total_channels = 0;
        int online_count = 0;
    };

    MENodeId next_id_ = 1;
    std::unordered_map<MENodeId, MENode> nodes_;
    std::unordered_map<MENodeId, Impl> node_data_;
    std::vector<std::pair<MENodeId, MENodeId>> edges_;
    std::unordered_map<MENodeId, ChannelProviderState> channel_providers_;
    std::unordered_map<int, ComponentInfo> component_info_;

    int component_of(MENodeId id) const;
    const Impl* node_impl(MENodeId id) const;
    Impl* node_impl(MENodeId id);
    std::vector<MENodeId> nodes_in_component(int comp_id) const;
    void allocate_channels();
    bool is_device_type(MENodeType type) const;
};

} // namespace science_and_theology::gt
