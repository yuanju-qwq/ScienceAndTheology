// Game-owned topology and scalar-field simulation for connected regions.
//
// This module owns the typed boundary between world topology producers
// (power, fluid, pollution, and temperature) and authoritative fixed-tick
// simulation. It intentionally does not depend on WorldData, Godot, or a
// transport/event-bus implementation.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

enum class RegionDomain : uint8_t {
    kPower = 0,
    kFluid,
    kPollution,
    kTemperature,
    kCount,
};

[[nodiscard]] constexpr bool is_region_domain(RegionDomain domain) noexcept {
    return static_cast<uint8_t>(domain) < static_cast<uint8_t>(RegionDomain::kCount);
}

[[nodiscard]] std::string_view region_domain_name(RegionDomain domain) noexcept;

using RegionId = uint64_t;
constexpr RegionId kInvalidRegionId = 0;

// A world-space node supplied by one typed topology producer. Edges are
// explicit: face-adjacent nodes can remain distinct regions, which is needed
// for pollution and temperature exchange across a boundary.
struct RegionNode {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;

    [[nodiscard]] bool operator==(const RegionNode& other) const noexcept {
        return dimension_id == other.dimension_id && block_x == other.block_x &&
               block_y == other.block_y && block_z == other.block_z;
    }
};

struct RegionNodeHash {
    [[nodiscard]] size_t operator()(const RegionNode& node) const noexcept;
};

// Region IDs are only unique within a domain. Keeping the domain in every
// handle prevents a power ID from being accidentally used as a fluid or
// pollution region ID at the public API boundary.
struct RegionHandle {
    RegionDomain domain = RegionDomain::kCount;
    RegionId id = kInvalidRegionId;

    [[nodiscard]] bool valid() const noexcept {
        return is_region_domain(domain) && id != kInvalidRegionId;
    }
};

struct RegionState {
    RegionId id = kInvalidRegionId;
    RegionDomain domain = RegionDomain::kCount;
    std::string dimension_id;
    size_t node_count = 0;

    // Pollution is a normalized concentration. Temperature is Celsius.
    // They are meaningful only for their matching scalar-field domains.
    double pollution = 0.0;
    double temperature_celsius = 20.0;
};

enum class RegionTopologyEventKind : uint8_t {
    kCreated,
    kMerged,
    kSplit,
    kDestroyed,
};

// For a merge, region is the surviving component and related_region is the
// absorbed component. For a split, region is the retained component and
// related_region is the newly created component.
struct RegionTopologyEvent {
    RegionTopologyEventKind kind = RegionTopologyEventKind::kCreated;
    RegionHandle region;
    RegionHandle related_region;
    std::string dimension_id;
};

// Server replication, power, fluid, and future persistence bridges may
// observe committed topology changes through this narrow game-owned boundary.
// Implementations must not mutate RegionTopology from inside the callback.
class IRegionTopologyEventSink {
public:
    virtual ~IRegionTopologyEventSink() = default;

    virtual void on_region_topology_event(const RegionTopologyEvent& event) = 0;
};

class RegionTopology {
public:
    RegionTopology();
    ~RegionTopology();

    RegionTopology(const RegionTopology&) = delete;
    RegionTopology& operator=(const RegionTopology&) = delete;
    RegionTopology(RegionTopology&&) = delete;
    RegionTopology& operator=(RegionTopology&&) = delete;

    void set_event_sink(IRegionTopologyEventSink* event_sink) noexcept;

    // Registers one node in a domain. Adding an already registered node is
    // idempotent and returns its current component handle.
    [[nodiscard]] RegionHandle add_node(RegionDomain domain, const RegionNode& node);
    [[nodiscard]] bool remove_node(RegionDomain domain, const RegionNode& node);

    // Adds an explicit same-dimension edge. A successful connection merges
    // the two components immediately; a later removal detects any split on
    // the next authoritative fixed tick.
    [[nodiscard]] RegionHandle connect(RegionDomain domain,
                                       const RegionNode& first,
                                       const RegionNode& second);
    // Removes one explicit edge and defers split reconciliation to the next
    // authoritative fixed tick. This lets a topology producer change a
    // connection without unregistering either endpoint.
    [[nodiscard]] bool disconnect(RegionDomain domain,
                                  const RegionNode& first,
                                  const RegionNode& second);

    [[nodiscard]] std::optional<RegionHandle> region_for_node(
        RegionDomain domain, const RegionNode& node) const;
    [[nodiscard]] const RegionState* find_state(RegionHandle handle) const;
    [[nodiscard]] size_t region_count(RegionDomain domain) const noexcept;
    [[nodiscard]] size_t node_count(RegionDomain domain) const noexcept;

    // Scalar fields are intentionally domain-specific. Calls with a handle
    // from another domain fail instead of silently mutating unrelated state.
    [[nodiscard]] bool set_pollution(RegionHandle handle, double concentration);
    [[nodiscard]] bool set_temperature(RegionHandle handle, double celsius);

    // Runs topology split reconciliation and scalar diffusion exactly once
    // for an increasing authoritative tick index. Returns false for duplicate
    // or stale calls, protecting the global topology from chunk-loop repeats.
    [[nodiscard]] bool fixed_tick(uint64_t source_tick);
    [[nodiscard]] std::optional<uint64_t> last_fixed_tick() const noexcept {
        return last_fixed_tick_;
    }

    // Releases all transient topology without emitting destruction events.
    // Session shutdown uses this after detaching its host-owned sink.
    void clear() noexcept;

private:
    struct Graph;

    [[nodiscard]] Graph* graph(RegionDomain domain) noexcept;
    [[nodiscard]] const Graph* graph(RegionDomain domain) const noexcept;
    [[nodiscard]] RegionId find_root(const Graph& graph, RegionId id) const noexcept;
    [[nodiscard]] RegionId unite(Graph& graph, RegionId first, RegionId second) noexcept;
    void rebuild_dirty_regions(RegionDomain domain, Graph& graph);
    void diffuse_scalar_field(RegionDomain domain, double rate, bool clamp_to_unit_interval);
    [[nodiscard]] std::vector<RegionId> neighboring_regions(
        RegionDomain domain, const Graph& graph, RegionId region_id) const;
    void notify(const RegionTopologyEvent& event) const;

    static constexpr size_t kDomainCount = static_cast<size_t>(RegionDomain::kCount);
    std::array<std::unique_ptr<Graph>, kDomainCount> graphs_;
    IRegionTopologyEventSink* event_sink_ = nullptr;
    std::optional<uint64_t> last_fixed_tick_;
};

}  // namespace snt::game
