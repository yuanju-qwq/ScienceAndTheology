// Offline industrial-network islands.
//
// The provider owns one atomic unload boundary across cable and pipe graphs,
// then records separate resource segments inside that boundary. The simulator
// executes explicit power and item transfers before delegating machine work to
// the shared execution core. Fluid segments deliberately keep their component
// materialized until a fluid simulator is bound.

#pragma once

#include "game/resources/resource_key.h"
#include "game/simulation/offline_machine_simulation.h"

namespace snt::game {

[[nodiscard]] inline const ResourceKey& offline_network_power_ledger_key() {
    static const ResourceKey key = ResourceKey::power("snt.power.buffer");
    return key;
}

class OfflineIndustrialNetworkIslandProvider final : public IOfflineNetworkIslandProvider {
public:
    OfflineIndustrialNetworkIslandProvider(GameContentRegistry& content,
                                           GameChunkSidecarRegistry& sidecars) noexcept;

    [[nodiscard]] snt::core::Expected<std::vector<OfflineNetworkIslandSnapshot>>
    build_offline_islands(std::span<const ChunkKey> candidate_chunks,
                          uint64_t source_tick) override;

private:
    GameContentRegistry* content_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
};

class OfflineIndustrialNetworkIslandSimulator final : public IOfflineNetworkIslandSimulator {
public:
    [[nodiscard]] snt::core::Expected<uint64_t> advance_offline_island(
        OfflineNetworkIslandSnapshot& snapshot,
        GameContentRegistry& content,
        GameChunkSidecarRegistry& sidecars,
        IMachineTickEventSink* event_sink,
        uint64_t first_tick,
        uint64_t tick_count) override;
};

}  // namespace snt::game
