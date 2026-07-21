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

// Persistent network-island contracts. They intentionally contain only
// compressed endpoints, resource ledgers, and chunk borders, never terrain or
// per-pipe ECS state. A topology provider will own their construction later.
enum class OfflineNetworkResourceKind : uint8_t {
    kPower = 0,
    kItem = 1,
    kFluid = 2,
};

struct OfflineNetworkResourceLedger {
    OfflineNetworkResourceKind kind = OfflineNetworkResourceKind::kPower;
    std::string resource_id;
    int64_t stored_amount = 0;
    int64_t capacity = 0;
};

struct OfflineNetworkBoundaryPort {
    uint64_t node_id = 0;
    ChunkKey adjacent_chunk;
    uint8_t direction = 0;
    uint64_t topology_revision = 0;
};

struct OfflineNetworkIslandSnapshot {
    uint64_t island_id = 0;
    std::string dimension_id;
    uint64_t topology_revision = 0;
    uint64_t last_simulated_tick = 0;
    std::vector<uint64_t> machine_guids;
    std::vector<OfflineNetworkBoundaryPort> boundary_ports;
    std::vector<OfflineNetworkResourceLedger> ledgers;
};

class IOfflineNetworkIslandProvider {
public:
    virtual ~IOfflineNetworkIslandProvider() = default;

    // Must return a complete, immutable snapshot for all requested chunks or
    // reject the transition. The first implementation deliberately has no
    // provider and leaves kNetworkIsland machines paused.
    [[nodiscard]] virtual snt::core::Expected<OfflineNetworkIslandSnapshot>
    build_offline_island(std::span<const ChunkKey> chunks,
                         uint64_t source_tick) = 0;
};

struct OfflineMachineSimulationConfig {
    uint32_t default_max_batch_ticks = 1200;
};

struct OfflineChunkMachineTransition {
    size_t standalone_machine_count = 0;
    size_t paused_machine_count = 0;
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

    [[nodiscard]] MachineRuntimePersistenceRecord* find_record(
        const OfflineLocation& location, uint64_t entity_guid) noexcept;
    [[nodiscard]] snt::core::Expected<void> advance_record(
        MachineRuntimePersistenceRecord& record, uint64_t current_tick);
    void index_offline_record(const ChunkKey& chunk_key,
                              MachineRuntimePersistenceRecord& record);
    void schedule_record(const MachineRuntimePersistenceRecord& record);
    void erase_offline_record(uint64_t entity_guid) noexcept;
    [[nodiscard]] uint32_t batch_ticks_for(const MachineDefinition& definition) const noexcept;
    [[nodiscard]] bool should_schedule(const MachineRuntimePersistenceRecord& record,
                                       const MachineDefinition& definition) const noexcept;

    GameContentRegistry* content_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    OfflineMachineSimulationConfig config_;
    IMachineTickEventSink* event_sink_ = nullptr;
    std::map<uint64_t, OfflineLocation> offline_locations_;
    std::priority_queue<ScheduledMachine,
                        std::vector<ScheduledMachine>,
                        ScheduledMachineLater> schedule_;
    uint64_t last_tick_ = 0;
    std::optional<uint64_t> last_catch_up_log_tick_;
};

}  // namespace snt::game
