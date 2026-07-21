// Offline coarse machine simulation.
//
// This module owns the non-ECS state of machines whose chunks are no longer
// active. It advances only content-approved standalone machines today; the
// network-island value types below reserve the durable boundary for future
// power, item, and fluid topology producers.

#pragma once

#include "core/expected.h"
#include "ecs/entity_guid.h"
#include "game/client/game_content_registry.h"
#include "game/simulation/offline_network_island_registry.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::game {

class IMachineTickEventSink;

class IOfflineNetworkIslandProvider {
public:
    virtual ~IOfflineNetworkIslandProvider() = default;

    // Builds every complete island that can be detached from `candidate_chunks`.
    // A returned snapshot may never include a chunk outside that set: callers
    // keep an island materialized when a connected member is still ticketed.
    // Candidates not present in a returned snapshot remain paused instead of
    // receiving an invented partial network simulation.
    [[nodiscard]] virtual snt::core::Expected<std::vector<OfflineNetworkIslandSnapshot>>
    build_offline_islands(std::span<const ChunkKey> candidate_chunks,
                          uint64_t source_tick) = 0;
};

// Resource-specific island simulators operate only on durable sidecar values
// and a topology snapshot. Power is the first implementation; item, fluid,
// and signal simulators plug into the same ownership/scheduling boundary.
class IOfflineNetworkIslandSimulator {
public:
    virtual ~IOfflineNetworkIslandSimulator() = default;

    [[nodiscard]] virtual snt::core::Expected<uint64_t> advance_offline_island(
        OfflineNetworkIslandSnapshot& snapshot,
        GameContentRegistry& content,
        GameChunkSidecarRegistry& sidecars,
        IMachineTickEventSink* event_sink,
        uint64_t first_tick,
        uint64_t tick_count) = 0;
};

struct OfflineMachineSimulationConfig {
    uint32_t default_max_batch_ticks = 1200;
};

struct OfflineChunkMachineTransition {
    size_t standalone_machine_count = 0;
    size_t paused_machine_count = 0;
    size_t network_island_machine_count = 0;
    size_t network_island_count = 0;
    size_t deferred_network_machine_count = 0;
};

class OfflineMachineSimulationService final {
public:
    OfflineMachineSimulationService(GameContentRegistry& content,
                                    GameChunkSidecarRegistry& sidecars,
                                    OfflineMachineSimulationConfig config = {}) noexcept;

    void set_event_sink(IMachineTickEventSink* event_sink) noexcept {
        event_sink_ = event_sink;
    }
    void set_network_island_provider(IOfflineNetworkIslandProvider* provider) noexcept {
        network_island_provider_ = provider;
    }
    void set_network_island_simulator(IOfflineNetworkIslandSimulator* simulator) noexcept {
        network_island_simulator_ = simulator;
    }

    // Rebuilds the sparse scheduler from durable offline records. Startup has
    // no wall-clock catch-up: persisted work resumes from this server tick.
    [[nodiscard]] snt::core::Expected<void> initialize(uint64_t current_tick);

    // Captures and destroys all materialized machine runtimes in one chunk.
    // Content policy selects standalone coarse simulation or a deterministic
    // paused record. Callers may remove terrain only after this succeeds.
    [[nodiscard]] snt::core::Expected<OfflineChunkMachineTransition>
    dematerialize_chunk(snt::ecs::World& world,
                        const ChunkKey& chunk_key,
                        uint64_t current_tick);

    // Transitions a complete non-ticketed chunk set in one transaction. This
    // is the network-safe entry point used by residency controllers: a power
    // island may be claimed only when every member chunk belongs to this set.
    [[nodiscard]] snt::core::Expected<OfflineChunkMachineTransition>
    dematerialize_chunks(snt::ecs::World& world,
                         std::span<const ChunkKey> chunk_keys,
                         uint64_t current_tick);

    // Flushes a chunk's offline records to current_tick, transfers ownership
    // back to ECS, and restores their materialized runtime components.
    [[nodiscard]] snt::core::Expected<void>
    materialize_chunk(snt::ecs::World& world,
                      const ChunkKey& chunk_key,
                      uint64_t current_tick);

    // Runs only due batches, never one task per offline machine per fixed tick.
    [[nodiscard]] snt::core::Expected<void> tick(uint64_t current_tick);
    [[nodiscard]] snt::core::Expected<void> flush(uint64_t current_tick);

    [[nodiscard]] size_t offline_machine_count() const noexcept {
        return offline_locations_.size();
    }
    [[nodiscard]] size_t offline_network_island_count() const noexcept {
        return network_islands_.size();
    }
    [[nodiscard]] uint64_t last_tick() const noexcept { return last_tick_; }

private:
    struct OfflineLocation {
        ChunkKey chunk_key;
        uint64_t epoch = 0;
    };

    struct ScheduledMachine {
        uint64_t due_tick = 0;
        uint64_t entity_guid = 0;
        uint64_t epoch = 0;
    };

    struct ScheduledMachineLater {
        bool operator()(const ScheduledMachine& left,
                        const ScheduledMachine& right) const noexcept {
            if (left.due_tick != right.due_tick) return left.due_tick > right.due_tick;
            if (left.entity_guid != right.entity_guid) {
                return left.entity_guid > right.entity_guid;
            }
            return left.epoch > right.epoch;
        }
    };

    struct ScheduledNetworkIsland {
        uint64_t due_tick = 0;
        uint64_t island_id = 0;
        uint64_t ownership_epoch = 0;
    };

    struct ScheduledNetworkIslandLater {
        bool operator()(const ScheduledNetworkIsland& left,
                        const ScheduledNetworkIsland& right) const noexcept {
            if (left.due_tick != right.due_tick) return left.due_tick > right.due_tick;
            if (left.island_id != right.island_id) return left.island_id > right.island_id;
            return left.ownership_epoch > right.ownership_epoch;
        }
    };

    [[nodiscard]] MachineRuntimePersistenceRecord* find_record(
        const OfflineLocation& location, uint64_t entity_guid) noexcept;
    [[nodiscard]] snt::core::Expected<void> advance_record(
        MachineRuntimePersistenceRecord& record, uint64_t current_tick);
    void index_offline_record(const ChunkKey& chunk_key,
                              MachineRuntimePersistenceRecord& record);
    void schedule_record(const MachineRuntimePersistenceRecord& record);
    [[nodiscard]] snt::core::Expected<void> advance_network_island(
        OfflineNetworkIslandSnapshot& snapshot,
        uint64_t current_tick);
    void schedule_network_island(const OfflineNetworkIslandSnapshot& snapshot);
    void erase_offline_record(uint64_t entity_guid) noexcept;
    [[nodiscard]] uint32_t batch_ticks_for(const MachineDefinition& definition) const noexcept;
    [[nodiscard]] uint32_t batch_ticks_for(const OfflineNetworkIslandSnapshot& snapshot) const noexcept;
    [[nodiscard]] bool should_schedule(const MachineRuntimePersistenceRecord& record,
                                       const MachineDefinition& definition) const noexcept;

    GameContentRegistry* content_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    OfflineMachineSimulationConfig config_;
    IMachineTickEventSink* event_sink_ = nullptr;
    IOfflineNetworkIslandProvider* network_island_provider_ = nullptr;
    IOfflineNetworkIslandSimulator* network_island_simulator_ = nullptr;
    OfflineNetworkIslandRegistry network_islands_;
    std::map<uint64_t, OfflineLocation> offline_locations_;
    std::priority_queue<ScheduledMachine,
                        std::vector<ScheduledMachine>,
                        ScheduledMachineLater> schedule_;
    std::priority_queue<ScheduledNetworkIsland,
                        std::vector<ScheduledNetworkIsland>,
                        ScheduledNetworkIslandLater> network_schedule_;
    uint64_t last_tick_ = 0;
    std::optional<uint64_t> last_catch_up_log_tick_;
};

}  // namespace snt::game
