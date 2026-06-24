#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flow_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// IContainerAccess — unified abstraction over connected containers
// ============================================================
//
// The Manager discovers containers via cables. Each container (furnace,
// chest, machine, fluid tank, energy hatch, ...) is wrapped in an
// IContainerAccess implementation that exposes item / fluid / energy /
// redstone capabilities uniformly.
//
// The executor operates purely through this interface, so adding support
// for a new container type only requires writing a new adapter — the
// executor and node logic remain unchanged.
//
// Not every container supports every capability. Use has_items() /
// has_fluids() / has_energy() / has_redstone() to check before calling
// the corresponding methods.

class IContainerAccess {
public:
    virtual ~IContainerAccess() = default;

    // --- Identity ---
    virtual ContainerId get_id() const = 0;
    virtual std::string get_display_name() const = 0;

    // --- Capability flags ---
    virtual bool has_items() const { return false; }
    virtual bool has_fluids() const { return false; }
    virtual bool has_energy() const { return false; }
    virtual bool has_redstone() const { return false; }

    // --- Item inventory ---
    // Returns how many of item_id are currently stored.
    virtual int64_t count_item(gt::ItemId item_id) const { (void)item_id; return 0; }
    // Returns total item count across all slots.
    virtual int64_t count_total_items() const { return 0; }
    // Extracts up to 'count' items. Returns actual amount extracted.
    virtual int64_t extract_item(gt::ItemId item_id, int64_t count) {
        (void)item_id; (void)count; return 0;
    }
    // Inserts up to 'count' items. Returns actual amount inserted.
    virtual int64_t insert_item(gt::ItemId item_id, int64_t count) {
        (void)item_id; (void)count; return 0;
    }
    // Lists all distinct items currently stored (for inspection / filter UI).
    virtual std::vector<FlowItemEntry> list_items() const { return {}; }

    // --- Fluid tanks ---
    virtual int64_t count_fluid(gt::FluidId fluid_id) const { (void)fluid_id; return 0; }
    virtual int64_t extract_fluid(gt::FluidId fluid_id, int64_t amount_mb) {
        (void)fluid_id; (void)amount_mb; return 0;
    }
    virtual int64_t insert_fluid(gt::FluidId fluid_id, int64_t amount_mb) {
        (void)fluid_id; (void)amount_mb; return 0;
    }
    virtual std::vector<FlowFluidEntry> list_fluids() const { return {}; }

    // --- Energy ---
    virtual int64_t get_energy_stored() const { return 0; }
    virtual int64_t get_energy_capacity() const { return 0; }
    virtual int64_t extract_energy(int64_t amount) { (void)amount; return 0; }
    virtual int64_t insert_energy(int64_t amount) { (void)amount; return 0; }

    // --- Redstone ---
    virtual int32_t get_redstone_signal() const { return 0; }
    virtual void set_redstone_signal(int32_t signal) { (void)signal; }
};

// ============================================================
// ContainerRegistry — maps ContainerId → IContainerAccess
// ============================================================
//
// Owned by the SFMManager. Populated during cable-based container
// discovery. The executor looks up containers by index (the
// "container_index" param on I/O nodes) into this registry.

class ContainerRegistry {
public:
    ContainerRegistry() = default;

    // Registers a container. Takes ownership of the pointer.
    // Returns the assigned ContainerId.
    ContainerId register_container(std::unique_ptr<IContainerAccess> access);

    bool unregister_container(ContainerId id);

    IContainerAccess* get_container(ContainerId id) const {
        auto it = containers_.find(id);
        return it == containers_.end() ? nullptr : it->second.get();
    }

    // Index-based access (1-based, matching node "container_index" param).
    // Index 0 means "invalid / not set".
    IContainerAccess* get_by_index(uint32_t index) const;

    size_t size() const { return containers_.size(); }

    // Returns a list of (index, display_name) for UI display.
    std::vector<std::pair<uint32_t, std::string>> list_containers() const;

    void clear() { containers_.clear(); id_to_index_.clear(); index_to_id_.clear(); next_id_ = 1; }

private:
    std::unordered_map<ContainerId, std::unique_ptr<IContainerAccess>> containers_;
    // Bidirectional mapping between ContainerId and 1-based index.
    std::unordered_map<ContainerId, uint32_t> id_to_index_;
    std::unordered_map<uint32_t, ContainerId> index_to_id_;
    ContainerId next_id_ = 1;
};

} // namespace science_and_theology::sfm
