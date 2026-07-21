// Cable-backed offline power islands.
//
// The topology provider compresses only complete cable components into a
// durable island snapshot. The simulator then moves explicitly permitted
// buffer energy and delegates machine progress to the shared execution core.

#pragma once

#include "game/simulation/offline_machine_simulation.h"

#include <string_view>

namespace snt::game {

inline constexpr std::string_view kOfflinePowerLedgerResourceId = "snt.power.buffer";

class OfflinePowerNetworkIslandProvider final : public IOfflineNetworkIslandProvider {
public:
    OfflinePowerNetworkIslandProvider(GameContentRegistry& content,
                                      GameChunkSidecarRegistry& sidecars) noexcept;

    [[nodiscard]] snt::core::Expected<std::vector<OfflineNetworkIslandSnapshot>>
    build_offline_islands(std::span<const ChunkKey> candidate_chunks,
                          uint64_t source_tick) override;

private:
    GameContentRegistry* content_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
};

class OfflinePowerNetworkIslandSimulator final : public IOfflineNetworkIslandSimulator {
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
