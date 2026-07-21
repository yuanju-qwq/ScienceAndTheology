// Offline coarse machine simulation implementation.

#define SNT_LOG_CHANNEL "game.offline_machine"
#include "game/simulation/offline_machine_simulation.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_offline(const MachineRuntimePersistenceRecord& record) noexcept {
    return record.residency == MachineRuntimeResidency::kOfflineStandalone ||
           record.residency == MachineRuntimeResidency::kOfflineNetworkIsland;
}

[[nodiscard]] std::string describe_chunk(const ChunkKey& key) {
    return key.dimension_id + " (" + std::to_string(key.chunk_x) + "," +
           std::to_string(key.chunk_y) + "," + std::to_string(key.chunk_z) + ")";
}

}  // namespace

OfflineMachineSimulationService::OfflineMachineSimulationService(
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars,
    OfflineMachineSimulationConfig config) noexcept
    : content_(&content), sidecars_(&sidecars), config_(config) {}

snt::core::Expected<void> OfflineMachineSimulationService::initialize(uint64_t current_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline machine simulation service is unavailable");
    }
    if (config_.default_max_batch_ticks == 0) {
        return invalid_argument("Offline machine simulation default batch ticks must be positive");
    }

    offline_locations_.clear();
    schedule_ = {};
    size_t recovered = 0;
    size_t deferred_network = 0;
    sidecars_->for_each([&](const ChunkKey& chunk_key, GameChunkSidecar& sidecar) {
        for (MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (!is_offline(record)) continue;
            // Server downtime is not gameplay time. The persisted state was
            // flushed on shutdown, so begin a new scheduler epoch at zero or
            // the supplied authoritative tick without wall-clock catch-up.
            record.offline_last_simulated_tick = current_tick;
            if (record.residency == MachineRuntimeResidency::kOfflineNetworkIsland) {
                ++deferred_network;
                continue;
            }
            index_offline_record(chunk_key, record);
            ++recovered;
        }
    });
    last_tick_ = current_tick;
    if (recovered != 0 || deferred_network != 0) {
        SNT_LOG_INFO("Initialized %zu standalone offline machine(s), deferred %zu network-island machine(s)",
                     recovered, deferred_network);
    }
    return {};
}

snt::core::Expected<OfflineChunkMachineTransition>
OfflineMachineSimulationService::dematerialize_chunk(
    snt::ecs::World& world,
    const ChunkKey& chunk_key,
    uint64_t current_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline machine simulation service is unavailable");
    }
    if (current_tick < last_tick_) {
        return invalid_argument("Offline machine chunk transition received a stale tick");
    }
    GameChunkSidecar* sidecar = sidecars_->get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot dematerialize machines without a chunk sidecar");
    }

    struct Decision {
        size_t record_index = 0;
        MachineRuntimeResidency residency = MachineRuntimeResidency::kPaused;
    };
    std::vector<Decision> decisions;
    OfflineChunkMachineTransition transition;
    for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
        const MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[index];
        if (record.residency != MachineRuntimeResidency::kLoaded) continue;
        const MachineDefinition* definition = content_->find_machine(record.machine_id);
        if (definition == nullptr ||
            definition->offline_simulation.mode == MachineOfflineSimulationMode::kDisabled) {
            ++transition.paused_machine_count;
            decisions.push_back({index, MachineRuntimeResidency::kPaused});
            continue;
        }
        if (definition->offline_simulation.mode == MachineOfflineSimulationMode::kStandalone) {
            ++transition.standalone_machine_count;
            decisions.push_back({index, MachineRuntimeResidency::kOfflineStandalone});
            continue;
        }

        // No pipe/cable topology producer exists yet. The declared profile is
        // retained in content, but this machine must not receive invented
        // resource transfers while its factory graph is unavailable.
        ++transition.deferred_network_machine_count;
        decisions.push_back({index, MachineRuntimeResidency::kPaused});
    }
    if (decisions.empty()) {
        last_tick_ = current_tick;
        return transition;
    }

    if (auto result = GameMachineRuntimePersistence::capture_chunk(
            world, *sidecars_, chunk_key); !result) {
        return result.error();
    }
    if (auto result = GameMachineRuntimePersistence::destroy_chunk_runtimes(
            world, *sidecars_, chunk_key); !result) {
        return result.error();
    }

    for (const Decision& decision : decisions) {
        MachineRuntimePersistenceRecord& record =
            sidecar->machine_runtime_records[decision.record_index];
        record.residency = decision.residency;
        record.offline_last_simulated_tick = current_tick;
        record.offline_island_id = 0;
        ++record.offline_epoch;
        if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
            index_offline_record(chunk_key, record);
        }
    }
    last_tick_ = current_tick;
    SNT_LOG_INFO("Dematerialized machine chunk %s: standalone=%zu paused=%zu network_deferred=%zu",
                 describe_chunk(chunk_key).c_str(), transition.standalone_machine_count,
                 transition.paused_machine_count, transition.deferred_network_machine_count);
    return transition;
}

snt::core::Expected<void> OfflineMachineSimulationService::materialize_chunk(
    snt::ecs::World& world,
    const ChunkKey& chunk_key,
    uint64_t current_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline machine simulation service is unavailable");
    }
    if (current_tick < last_tick_) {
        return invalid_argument("Offline machine chunk transition received a stale tick");
    }
    GameChunkSidecar* sidecar = sidecars_->get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot materialize machines without a chunk sidecar");
    }

    for (MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
        if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
            if (auto result = advance_record(record, current_tick); !result) return result.error();
        }
    }

    struct PreviousOwnership {
        size_t record_index = 0;
        MachineRuntimeResidency residency = MachineRuntimeResidency::kPaused;
        uint64_t offline_island_id = 0;
    };
    std::vector<PreviousOwnership> previous;
    for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
        MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[index];
        if (record.residency == MachineRuntimeResidency::kLoaded) continue;
        previous.push_back({index, record.residency, record.offline_island_id});
        erase_offline_record(record.entity_guid);
        record.residency = MachineRuntimeResidency::kLoaded;
        record.offline_island_id = 0;
        ++record.offline_epoch;
    }
    if (previous.empty()) {
        last_tick_ = current_tick;
        return {};
    }

    if (auto result = GameMachineRuntimePersistence::restore_chunk(
            world, *sidecars_, chunk_key); !result) {
        for (const PreviousOwnership& ownership : previous) {
            MachineRuntimePersistenceRecord& record =
                sidecar->machine_runtime_records[ownership.record_index];
            record.residency = ownership.residency;
            record.offline_island_id = ownership.offline_island_id;
            ++record.offline_epoch;
            if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
                index_offline_record(chunk_key, record);
            }
        }
        return result.error();
    }
    last_tick_ = current_tick;
    SNT_LOG_INFO("Materialized %zu machine runtime(s) for chunk %s",
                 previous.size(), describe_chunk(chunk_key).c_str());
    return {};
}

snt::core::Expected<void> OfflineMachineSimulationService::tick(uint64_t current_tick) {
    if (current_tick < last_tick_) {
        return invalid_argument("Offline machine simulation tick regressed");
    }
    while (!schedule_.empty() && schedule_.top().due_tick <= current_tick) {
        const ScheduledMachine scheduled = schedule_.top();
        schedule_.pop();
        const auto found = offline_locations_.find(scheduled.entity_guid);
        if (found == offline_locations_.end() || found->second.epoch != scheduled.epoch) continue;
        MachineRuntimePersistenceRecord* record = find_record(found->second, scheduled.entity_guid);
        if (record == nullptr || record->offline_epoch != scheduled.epoch ||
            record->residency != MachineRuntimeResidency::kOfflineStandalone) {
            erase_offline_record(scheduled.entity_guid);
            continue;
        }
        if (auto result = advance_record(*record, current_tick); !result) return result.error();
        if (record->residency == MachineRuntimeResidency::kOfflineStandalone) {
            schedule_record(*record);
        } else {
            erase_offline_record(scheduled.entity_guid);
        }
    }
    last_tick_ = current_tick;
    return {};
}

snt::core::Expected<void> OfflineMachineSimulationService::flush(uint64_t current_tick) {
    if (current_tick < last_tick_) {
        return invalid_argument("Offline machine simulation flush regressed");
    }
    std::vector<uint64_t> entity_guids;
    entity_guids.reserve(offline_locations_.size());
    for (const auto& [entity_guid, location] : offline_locations_) {
        static_cast<void>(location);
        entity_guids.push_back(entity_guid);
    }
    for (const uint64_t entity_guid : entity_guids) {
        const auto found = offline_locations_.find(entity_guid);
        if (found == offline_locations_.end()) continue;
        MachineRuntimePersistenceRecord* record = find_record(found->second, entity_guid);
        if (record == nullptr || record->residency != MachineRuntimeResidency::kOfflineStandalone) {
            erase_offline_record(entity_guid);
            continue;
        }
        if (auto result = advance_record(*record, current_tick); !result) return result.error();
        if (record->residency != MachineRuntimeResidency::kOfflineStandalone) {
            erase_offline_record(entity_guid);
        }
    }
    schedule_ = {};
    for (const auto& [entity_guid, location] : offline_locations_) {
        MachineRuntimePersistenceRecord* record = find_record(location, entity_guid);
        if (record != nullptr) schedule_record(*record);
    }
    last_tick_ = current_tick;
    return {};
}

MachineRuntimePersistenceRecord* OfflineMachineSimulationService::find_record(
    const OfflineLocation& location, uint64_t entity_guid) noexcept {
    if (sidecars_ == nullptr) return nullptr;
    GameChunkSidecar* sidecar = sidecars_->get(location.chunk_key);
    if (sidecar == nullptr) return nullptr;
    const auto found = std::find_if(
        sidecar->machine_runtime_records.begin(), sidecar->machine_runtime_records.end(),
        [entity_guid](const MachineRuntimePersistenceRecord& record) {
            return record.entity_guid == entity_guid;
        });
    return found == sidecar->machine_runtime_records.end() ? nullptr : &*found;
}

snt::core::Expected<void> OfflineMachineSimulationService::advance_record(
    MachineRuntimePersistenceRecord& record, uint64_t current_tick) {
    if (record.residency != MachineRuntimeResidency::kOfflineStandalone) return {};
    if (current_tick < record.offline_last_simulated_tick) {
        return invalid_argument("Offline machine record tick regressed");
    }
    const MachineDefinition* definition = content_->find_machine(record.machine_id);
    if (definition == nullptr ||
        definition->offline_simulation.mode != MachineOfflineSimulationMode::kStandalone) {
        record.residency = MachineRuntimeResidency::kPaused;
        SNT_LOG_WARN("Paused offline machine %llu because its standalone content profile is unavailable",
                     static_cast<unsigned long long>(record.entity_guid));
        return {};
    }
    const uint64_t available_ticks = current_tick - record.offline_last_simulated_tick;
    if (available_ticks == 0) return {};
    // A scheduler delay must not turn one due record into an unbounded
    // execution slice. tick() requeues the next due batch when the machine can
    // still run, so deterministic catch-up remains ordered by server ticks.
    const uint64_t elapsed = std::min<uint64_t>(available_ticks, batch_ticks_for(*definition));

    auto input = make_machine_execution_input(
        *content_, snt::ecs::EntityGuid{record.entity_guid},
        GameMachineRuntimePersistence::make_runtime_component(record));
    if (!input) {
        record.residency = MachineRuntimeResidency::kPaused;
        SNT_LOG_WARN("Paused offline machine %llu: %s",
                     static_cast<unsigned long long>(record.entity_guid),
                     input.error().format().c_str());
        return {};
    }
    input->allow_new_jobs = definition->offline_simulation.can_start_new_jobs &&
                             !definition->requires_manual_activation;
    MachineExecutionResult result = advance_machine_execution(
        std::move(*input), record.offline_last_simulated_tick + 1, elapsed);
    GameMachineRuntimePersistence::copy_runtime_to_record(record, result.machine);
    const bool reschedule = should_schedule(record, *definition);
    if (reschedule) {
        record.offline_last_simulated_tick += result.advanced_ticks;
    } else {
        // No offline actor can change a paused, empty, or manual machine, so
        // consume the remaining interval without scheduling no-op batches.
        record.offline_last_simulated_tick = current_tick;
    }
    constexpr uint64_t kCatchUpLogIntervalTicks = 1200;
    if (available_ticks > elapsed &&
        (!last_catch_up_log_tick_ ||
         current_tick - *last_catch_up_log_tick_ >= kCatchUpLogIntervalTicks)) {
        SNT_LOG_INFO("Offline machine %llu capped catch-up from %llu to %llu tick(s), executed=%llu, reschedule=%s",
                     static_cast<unsigned long long>(record.entity_guid),
                     static_cast<unsigned long long>(available_ticks),
                     static_cast<unsigned long long>(elapsed),
                     static_cast<unsigned long long>(result.advanced_ticks),
                     reschedule ? "true" : "false");
        last_catch_up_log_tick_ = current_tick;
    }
    if (event_sink_ != nullptr) {
        for (const MachineTickEvent& event : result.events) {
            event_sink_->on_machine_tick_event(event);
        }
    }
    return {};
}

void OfflineMachineSimulationService::index_offline_record(
    const ChunkKey& chunk_key,
    MachineRuntimePersistenceRecord& record) {
    offline_locations_.insert_or_assign(
        record.entity_guid, OfflineLocation{chunk_key, record.offline_epoch});
    schedule_record(record);
}

void OfflineMachineSimulationService::schedule_record(
    const MachineRuntimePersistenceRecord& record) {
    if (content_ == nullptr || record.residency != MachineRuntimeResidency::kOfflineStandalone) {
        return;
    }
    const MachineDefinition* definition = content_->find_machine(record.machine_id);
    if (definition == nullptr || !should_schedule(record, *definition)) return;
    const uint64_t batch_ticks = batch_ticks_for(*definition);
    const uint64_t due_tick = record.offline_last_simulated_tick >
            std::numeric_limits<uint64_t>::max() - batch_ticks
        ? std::numeric_limits<uint64_t>::max()
        : record.offline_last_simulated_tick + batch_ticks;
    schedule_.push({due_tick, record.entity_guid, record.offline_epoch});
}

void OfflineMachineSimulationService::erase_offline_record(uint64_t entity_guid) noexcept {
    offline_locations_.erase(entity_guid);
}

uint32_t OfflineMachineSimulationService::batch_ticks_for(
    const MachineDefinition& definition) const noexcept {
    return definition.offline_simulation.max_batch_ticks != 0
        ? definition.offline_simulation.max_batch_ticks
        : config_.default_max_batch_ticks;
}

bool OfflineMachineSimulationService::should_schedule(
    const MachineRuntimePersistenceRecord& record,
    const MachineDefinition& definition) const noexcept {
    if (definition.offline_simulation.mode != MachineOfflineSimulationMode::kStandalone) {
        return false;
    }
    if (record.run_state == static_cast<uint8_t>(MachineRunState::WaitingForEnergy) ||
        record.run_state == static_cast<uint8_t>(MachineRunState::WaitingForOutput) ||
        record.run_state == static_cast<uint8_t>(MachineRunState::NoMatchingRecipe) ||
        record.run_state == static_cast<uint8_t>(MachineRunState::WaitingForActivation)) {
        return false;
    }
    if (record.active_recipe) return true;
    return !record.input_slots.empty() &&
           definition.offline_simulation.can_start_new_jobs &&
           !definition.requires_manual_activation;
}

}  // namespace snt::game
