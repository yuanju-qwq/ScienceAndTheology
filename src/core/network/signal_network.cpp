#include "signal_network.hpp"

#include <algorithm>

namespace science_and_theology::gt {

// --- Signal sources ---

void SignalNetwork::set_source(MapPosition pos, int32_t strength) {
    sources_[pos] = strength;
}

void SignalNetwork::remove_source(MapPosition pos) {
    sources_.erase(pos);
}

int32_t SignalNetwork::get_source_strength(MapPosition pos) const {
    auto it = sources_.find(pos);
    return it == sources_.end() ? 0 : it->second;
}

// --- Network recomputation ---

// Recomputes signal distribution.
// For each wire component:
//   1. Find all sources adjacent to any wire in the component.
//   2. Component signal = max source strength (0 if no sources).
//   3. Assign that signal to every wire in the component.
//   4. Assign that signal to every adjacent non-wire position
//      (consumers), so they can read it via get_signal_at().
void SignalNetwork::update_network() {
    signal_map_.clear();

    // Index sources for fast lookup.
    // A source feeds a component if it is adjacent to a wire in it.
    // We iterate components, then for each wire check its 6 neighbors
    // against the source map.

    std::vector<std::vector<MapPosition>> components = graph_.find_all_components();

    for (const std::vector<MapPosition>& component : components) {
        // Phase 1: collect max source strength feeding this component.
        int32_t component_signal = 0;
        for (const MapPosition& wire : component) {
            // Check 6 neighbors of this wire for sources.
            // +X
            { MapPosition n{wire.x + 1, wire.y, wire.z};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
            // -X
            { MapPosition n{wire.x - 1, wire.y, wire.z};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
            // +Y
            { MapPosition n{wire.x, wire.y + 1, wire.z};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
            // -Y
            { MapPosition n{wire.x, wire.y - 1, wire.z};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
            // +Z
            { MapPosition n{wire.x, wire.y, wire.z + 1};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
            // -Z
            { MapPosition n{wire.x, wire.y, wire.z - 1};
              auto it = sources_.find(n);
              if (it != sources_.end()) component_signal = std::max(component_signal, it->second); }
        }

        if (component_signal <= 0) {
            continue;  // No source feeds this component; leave it unpowered.
        }

        // Phase 2: assign signal to every wire in the component.
        for (const MapPosition& wire : component) {
            signal_map_[wire] = component_signal;
        }

        // Phase 3: assign signal to adjacent non-wire positions (consumers).
        // A consumer reads the max signal among all wire components
        // adjacent to it. Since a consumer may border multiple components,
        // we use max-merge into signal_map_.
        std::vector<MapPosition> adjacent = graph_.find_adjacent_outside(component);
        for (const MapPosition& consumer : adjacent) {
            auto it = signal_map_.find(consumer);
            if (it == signal_map_.end()) {
                signal_map_[consumer] = component_signal;
            } else {
                it->second = std::max(it->second, component_signal);
            }
        }
    }
}

// --- Signal queries ---

int32_t SignalNetwork::get_signal_at(MapPosition pos) const {
    auto it = signal_map_.find(pos);
    return it == signal_map_.end() ? 0 : it->second;
}

std::vector<std::pair<MapPosition, int32_t>>
SignalNetwork::powered_positions() const {
    std::vector<std::pair<MapPosition, int32_t>> result;
    result.reserve(signal_map_.size());
    for (const auto& entry : signal_map_) {
        if (entry.second > 0) {
            result.emplace_back(entry.first, entry.second);
        }
    }
    return result;
}

void SignalNetwork::clear() {
    graph_.clear();
    sources_.clear();
    signal_map_.clear();
}

} // namespace science_and_theology::gt
