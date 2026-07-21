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
#include <map>
#include <optional>
#include <set>
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

[[nodiscard]] bool chunk_key_less(const ChunkKey& left, const ChunkKey& right) noexcept {
    if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
    if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
    if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
    return left.chunk_z < right.chunk_z;
}

[[nodiscard]] bool same_chunk(const ChunkKey& left, const ChunkKey& right) noexcept {
    return left.dimension_id == right.dimension_id &&
           left.chunk_x == right.chunk_x &&
           left.chunk_y == right.chunk_y &&
           left.chunk_z == right.chunk_z;
}

[[nodiscard]] uint64_t increment_epoch(uint64_t value) noexcept {
    return value == std::numeric_limits<uint64_t>::max() ? 0 : value + 1;
}

}  // namespace

OfflineMachineSimulationService::OfflineMachineSimulationService(
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars,
    OfflineMachineSimulationConfig config) noexcept
    : content_(&content), sidecars_(&sidecars), config_(config), network_islands_(sidecars) {}

snt::core::Expected<void> OfflineMachineSimulationService::initialize(uint64_t current_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline machine simulation service is unavailable");
    }
    if (config_.default_max_batch_ticks == 0) {
        return invalid_argument("Offline machine simulation default batch ticks must be positive");
    }
    if (auto result = network_islands_.initialize(); !result) return result.error();

    offline_locations_.clear();
    schedule_ = {};
    network_schedule_ = {};
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
    for (const uint64_t island_id : network_islands_.island_ids()) {
        OfflineNetworkIslandSnapshot* const snapshot = network_islands_.find(island_id);
        if (snapshot == nullptr) {
            return invalid_state("Recovered offline network island is unavailable");
        }
        snapshot->last_simulated_tick = current_tick;
        schedule_network_island(*snapshot);
    }
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
    return dematerialize_chunks(world, std::span<const ChunkKey>{&chunk_key, 1}, current_tick);
}

snt::core::Expected<OfflineChunkMachineTransition>
OfflineMachineSimulationService::dematerialize_chunks(
    snt::ecs::World& world,
    std::span<const ChunkKey> chunk_keys,
    uint64_t current_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline machine simulation service is unavailable");
    }
    if (current_tick < last_tick_) {
        return invalid_argument("Offline machine chunk transition received a stale tick");
    }
    std::vector<ChunkKey> candidates(chunk_keys.begin(), chunk_keys.end());
    std::sort(candidates.begin(), candidates.end(), chunk_key_less);
    if (std::adjacent_find(candidates.begin(), candidates.end(), same_chunk) !=
        candidates.end()) {
        return invalid_argument("Offline machine chunk transition has duplicate chunk keys");
    }
    for (const ChunkKey& chunk_key : candidates) {
        if (sidecars_->get(chunk_key) == nullptr) {
            return invalid_state("Cannot dematerialize machines without a chunk sidecar");
        }
    }

    struct Decision {
        ChunkKey chunk_key;
        size_t record_index = 0;
        MachineRuntimeResidency residency = MachineRuntimeResidency::kPaused;
    };
    std::vector<Decision> decisions;
    std::vector<uint64_t> loaded_machine_guids;
    std::set<uint64_t> network_candidate_guids;
    OfflineChunkMachineTransition transition;
    for (const ChunkKey& chunk_key : candidates) {
        GameChunkSidecar* sidecar = sidecars_->get(chunk_key);
        for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
            const MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[index];
            if (record.residency != MachineRuntimeResidency::kLoaded) continue;
            loaded_machine_guids.push_back(record.entity_guid);
            const MachineDefinition* definition = content_->find_machine(record.machine_id);
            if (definition == nullptr ||
                definition->offline_simulation.mode == MachineOfflineSimulationMode::kDisabled) {
                ++transition.paused_machine_count;
                decisions.push_back({chunk_key, index, MachineRuntimeResidency::kPaused});
                continue;
            }
            if (definition->offline_simulation.mode == MachineOfflineSimulationMode::kStandalone) {
                ++transition.standalone_machine_count;
                decisions.push_back(
                    {chunk_key, index, MachineRuntimeResidency::kOfflineStandalone});
                continue;
            }
            network_candidate_guids.insert(record.entity_guid);
        }
    }
    if (loaded_machine_guids.empty()) {
        last_tick_ = current_tick;
        return transition;
    }

    std::vector<OfflineNetworkIslandSnapshot> snapshots;
    if (!network_candidate_guids.empty() && network_island_provider_ != nullptr) {
        auto built = network_island_provider_->build_offline_islands(candidates, current_tick);
        if (!built) return built.error();
        snapshots = std::move(*built);
    }

    std::set<uint64_t> claimed_network_guids;
    for (const OfflineNetworkIslandSnapshot& snapshot : snapshots) {
        for (const ChunkKey& member_chunk : snapshot.member_chunks) {
            const bool allowed = std::any_of(
                candidates.begin(), candidates.end(), [&member_chunk](const ChunkKey& candidate) {
                    return same_chunk(candidate, member_chunk);
                });
            if (!allowed) {
                return invalid_state(
                    "Offline network topology attempted to detach a ticketed chunk member");
            }
        }
        for (const uint64_t entity_guid : snapshot.machine_guids) {
            if (!network_candidate_guids.contains(entity_guid) ||
                !claimed_network_guids.insert(entity_guid).second) {
                return invalid_state(
                    "Offline network topology has an invalid or overlapping machine membership");
            }
        }
    }

    for (const ChunkKey& chunk_key : candidates) {
        if (auto result = GameMachineRuntimePersistence::capture_chunk(
                world, *sidecars_, chunk_key); !result) {
            return result.error();
        }
    }

    std::vector<OfflineNetworkIslandClaim> claims;
    claims.reserve(snapshots.size());
    for (OfflineNetworkIslandSnapshot& snapshot : snapshots) {
        auto claim = network_islands_.claim(std::move(snapshot), current_tick);
        if (!claim) {
            for (auto it = claims.rbegin(); it != claims.rend(); ++it) {
                if (auto rollback = network_islands_.rollback_claim(*it); !rollback) {
                    SNT_LOG_ERROR("Failed to roll back offline network island %llu: %s",
                                  static_cast<unsigned long long>(it->island_id),
                                  rollback.error().format().c_str());
                }
            }
            return claim.error();
        }
        const OfflineNetworkIslandSnapshot* const claimed_snapshot =
            network_islands_.find(claim->island_id);
        if (claimed_snapshot == nullptr) {
            for (auto it = claims.rbegin(); it != claims.rend(); ++it) {
                static_cast<void>(network_islands_.rollback_claim(*it));
            }
            static_cast<void>(network_islands_.rollback_claim(*claim));
            return invalid_state("Claimed offline network island is unavailable");
        }
        transition.network_island_machine_count += claimed_snapshot->machine_guids.size();
        ++transition.network_island_count;
        schedule_network_island(*claimed_snapshot);
        claims.push_back(std::move(*claim));
    }

    for (const uint64_t entity_guid : network_candidate_guids) {
        if (claimed_network_guids.contains(entity_guid)) continue;
        ++transition.deferred_network_machine_count;
        // A topology provider saw this candidate but could not form a whole
        // island because another member is still ticketed. Keep its runtime
        // materialized; pausing would split a live electrical graph. Without
        // any provider we retain the old conservative paused fallback.
        if (network_island_provider_ != nullptr) continue;
        for (const ChunkKey& chunk_key : candidates) {
            GameChunkSidecar* sidecar = sidecars_->get(chunk_key);
            for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
                if (sidecar->machine_runtime_records[index].entity_guid == entity_guid) {
                    decisions.push_back({chunk_key, index, MachineRuntimeResidency::kPaused});
                    ++transition.paused_machine_count;
                }
            }
        }
    }

    for (const Decision& decision : decisions) {
        GameChunkSidecar* sidecar = sidecars_->get(decision.chunk_key);
        if (sidecar == nullptr ||
            sidecar->machine_runtime_records[decision.record_index].offline_epoch ==
                std::numeric_limits<uint64_t>::max()) {
            return invalid_state("Offline machine ownership epoch is exhausted");
        }
    }

    std::sort(loaded_machine_guids.begin(), loaded_machine_guids.end());
    if (network_island_provider_ != nullptr) {
        loaded_machine_guids.erase(
            std::remove_if(loaded_machine_guids.begin(), loaded_machine_guids.end(),
                           [&network_candidate_guids, &claimed_network_guids](uint64_t entity_guid) {
                               return network_candidate_guids.contains(entity_guid) &&
                                      !claimed_network_guids.contains(entity_guid);
                           }),
            loaded_machine_guids.end());
    }
    if (auto result = GameMachineRuntimePersistence::destroy_runtimes(
            world, loaded_machine_guids); !result) {
        for (auto it = claims.rbegin(); it != claims.rend(); ++it) {
            if (auto rollback = network_islands_.rollback_claim(*it); !rollback) {
                SNT_LOG_ERROR("Failed to roll back offline network island %llu: %s",
                              static_cast<unsigned long long>(it->island_id),
                              rollback.error().format().c_str());
            }
        }
        return result.error();
    }

    for (const Decision& decision : decisions) {
        GameChunkSidecar* sidecar = sidecars_->get(decision.chunk_key);
        MachineRuntimePersistenceRecord& record =
            sidecar->machine_runtime_records[decision.record_index];
        record.residency = decision.residency;
        record.offline_last_simulated_tick = current_tick;
        record.offline_island_id = 0;
        ++record.offline_epoch;
        if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
            index_offline_record(decision.chunk_key, record);
        }
    }
    last_tick_ = current_tick;
    SNT_LOG_INFO("Dematerialized %zu machine chunk(s): standalone=%zu paused=%zu network=%zu island(s)/%zu machine(s), deferred=%zu",
                 candidates.size(), transition.standalone_machine_count,
                 transition.paused_machine_count, transition.network_island_count,
                 transition.network_island_machine_count,
                 transition.deferred_network_machine_count);
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
    if (sidecars_->get(chunk_key) == nullptr) {
        return invalid_state("Cannot materialize machines without a chunk sidecar");
    }

    // A network island is an atomic owner. If one member receives a ticket,
    // every member runtime returns to ECS before normal online topology is
    // allowed to observe it. Expand recursively because one chunk may anchor
    // several disjoint islands.
    std::vector<ChunkKey> materialized_chunks{chunk_key};
    std::map<uint64_t, OfflineNetworkIslandSnapshot> releases;
    for (size_t chunk_index = 0; chunk_index < materialized_chunks.size(); ++chunk_index) {
        GameChunkSidecar* sidecar = sidecars_->get(materialized_chunks[chunk_index]);
        if (sidecar == nullptr) {
            return invalid_state("Offline network island member chunk is unavailable");
        }
        for (const MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
            if (record.residency != MachineRuntimeResidency::kOfflineNetworkIsland) continue;
            const auto existing = releases.find(record.offline_island_id);
            if (existing != releases.end()) {
                if (existing->second.ownership_epoch != record.offline_epoch) {
                    return invalid_state("Offline network island has inconsistent member epochs");
                }
                continue;
            }
            OfflineNetworkIslandSnapshot* const live_snapshot =
                network_islands_.find(record.offline_island_id);
            if (live_snapshot == nullptr) {
                return invalid_state("Offline network island snapshot is unavailable");
            }
            if (auto result = advance_network_island(*live_snapshot, current_tick); !result) {
                return result.error();
            }
            auto release = network_islands_.prepare_release(
                record.offline_island_id, record.offline_epoch);
            if (!release) return release.error();
            for (const ChunkKey& member_chunk : release->member_chunks) {
                const bool already_added = std::any_of(
                    materialized_chunks.begin(), materialized_chunks.end(),
                    [&member_chunk](const ChunkKey& existing_chunk) {
                        return same_chunk(existing_chunk, member_chunk);
                    });
                if (!already_added) materialized_chunks.push_back(member_chunk);
            }
            releases.emplace(release->island_id, std::move(*release));
        }
    }
    std::sort(materialized_chunks.begin(), materialized_chunks.end(), chunk_key_less);

    struct PreviousOwnership {
        ChunkKey chunk_key;
        size_t record_index = 0;
        MachineRuntimeResidency residency = MachineRuntimeResidency::kPaused;
        uint64_t offline_island_id = 0;
        uint64_t offline_epoch = 0;
    };
    std::vector<PreviousOwnership> previous;
    for (const ChunkKey& materialized_chunk : materialized_chunks) {
        GameChunkSidecar* sidecar = sidecars_->get(materialized_chunk);
        if (sidecar == nullptr) {
            return invalid_state("Cannot materialize machines without a chunk sidecar");
        }
        for (MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
            if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
                if (auto result = advance_record(record, current_tick); !result) return result.error();
            }
        }
        for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
            MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[index];
            if (record.residency == MachineRuntimeResidency::kLoaded) continue;
            if (increment_epoch(record.offline_epoch) == 0) {
                return invalid_state("Offline machine ownership epoch is exhausted");
            }
            previous.push_back({materialized_chunk, index, record.residency,
                                record.offline_island_id, record.offline_epoch});
        }
    }
    if (previous.empty()) {
        last_tick_ = current_tick;
        return {};
    }

    for (const PreviousOwnership& ownership : previous) {
        GameChunkSidecar* sidecar = sidecars_->get(ownership.chunk_key);
        MachineRuntimePersistenceRecord& record =
            sidecar->machine_runtime_records[ownership.record_index];
        erase_offline_record(record.entity_guid);
        record.residency = MachineRuntimeResidency::kLoaded;
        record.offline_island_id = 0;
        record.offline_epoch = increment_epoch(record.offline_epoch);
    }

    std::optional<snt::core::Error> restore_error;
    for (const ChunkKey& materialized_chunk : materialized_chunks) {
        if (auto result = GameMachineRuntimePersistence::restore_chunk(
                world, *sidecars_, materialized_chunk); !result) {
            restore_error = result.error();
            break;
        }
    }
    if (restore_error.has_value()) {
        for (const PreviousOwnership& ownership : previous) {
            const GameChunkSidecar* sidecar = sidecars_->get(ownership.chunk_key);
            const uint64_t entity_guid = sidecar->machine_runtime_records[
                ownership.record_index].entity_guid;
            const entt::entity entity = world.find_entity_by_guid(
                snt::ecs::EntityGuid{entity_guid});
            if (entity != entt::null &&
                world.registry().all_of<MachineRuntimeComponent>(entity)) {
                world.destroy_entity(entity);
            }
        }
        for (const PreviousOwnership& ownership : previous) {
            GameChunkSidecar* sidecar = sidecars_->get(ownership.chunk_key);
            MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[
                ownership.record_index];
            record.residency = ownership.residency;
            record.offline_island_id = ownership.offline_island_id;
            record.offline_epoch = ownership.offline_epoch;
            if (record.residency == MachineRuntimeResidency::kOfflineStandalone) {
                index_offline_record(ownership.chunk_key, record);
            }
        }
        return *restore_error;
    }
    for (const auto& [island_id, release] : releases) {
        if (auto result = network_islands_.complete_release(
                island_id, release.ownership_epoch); !result) {
            return result.error();
        }
    }
    last_tick_ = current_tick;
    SNT_LOG_INFO("Materialized %zu machine runtime(s) across %zu chunk(s) for ticket %s",
                 previous.size(), materialized_chunks.size(), describe_chunk(chunk_key).c_str());
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
    while (!network_schedule_.empty() && network_schedule_.top().due_tick <= current_tick) {
        const ScheduledNetworkIsland scheduled = network_schedule_.top();
        network_schedule_.pop();
        OfflineNetworkIslandSnapshot* const snapshot =
            network_islands_.find(scheduled.island_id);
        if (snapshot == nullptr || snapshot->ownership_epoch != scheduled.ownership_epoch) {
            continue;
        }
        if (auto result = advance_network_island(*snapshot, current_tick); !result) {
            return result.error();
        }
        schedule_network_island(*snapshot);
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
    network_schedule_ = {};
    for (const uint64_t island_id : network_islands_.island_ids()) {
        OfflineNetworkIslandSnapshot* const snapshot = network_islands_.find(island_id);
        if (snapshot == nullptr) {
            return invalid_state("Offline network island is unavailable during flush");
        }
        if (auto result = advance_network_island(*snapshot, current_tick); !result) {
            return result.error();
        }
        schedule_network_island(*snapshot);
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

snt::core::Expected<void> OfflineMachineSimulationService::advance_network_island(
    OfflineNetworkIslandSnapshot& snapshot,
    uint64_t current_tick) {
    if (current_tick < snapshot.last_simulated_tick) {
        return invalid_argument("Offline network island tick regressed");
    }
    if (network_island_simulator_ == nullptr) {
        // A persisted island may be opened by a build that does not yet own a
        // resource simulator. Treat server downtime as non-gameplay time and
        // retain its state until a simulator is explicitly bound.
        snapshot.last_simulated_tick = current_tick;
        return {};
    }
    const uint64_t available_ticks = current_tick - snapshot.last_simulated_tick;
    if (available_ticks == 0) return {};
    const uint64_t elapsed = std::min<uint64_t>(
        available_ticks, batch_ticks_for(snapshot));
    auto advanced = network_island_simulator_->advance_offline_island(
        snapshot, *content_, *sidecars_, event_sink_,
        snapshot.last_simulated_tick + 1, elapsed);
    if (!advanced) return advanced.error();
    if (*advanced != elapsed) {
        return invalid_state("Offline network island simulator did not advance its complete batch");
    }
    snapshot.last_simulated_tick += elapsed;
    return {};
}

void OfflineMachineSimulationService::schedule_network_island(
    const OfflineNetworkIslandSnapshot& snapshot) {
    if (network_island_simulator_ == nullptr || snapshot.ownership_epoch == 0) return;
    const uint64_t batch_ticks = batch_ticks_for(snapshot);
    const uint64_t due_tick = snapshot.last_simulated_tick >
            std::numeric_limits<uint64_t>::max() - batch_ticks
        ? std::numeric_limits<uint64_t>::max()
        : snapshot.last_simulated_tick + batch_ticks;
    network_schedule_.push({due_tick, snapshot.island_id, snapshot.ownership_epoch});
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

uint32_t OfflineMachineSimulationService::batch_ticks_for(
    const OfflineNetworkIslandSnapshot& snapshot) const noexcept {
    uint32_t batch_ticks = config_.default_max_batch_ticks;
    if (sidecars_ == nullptr || content_ == nullptr) return batch_ticks;
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        const MachineRuntimePersistenceRecord* record = nullptr;
        for (const ChunkKey& chunk_key : snapshot.member_chunks) {
            const GameChunkSidecar* sidecar = sidecars_->get(chunk_key);
            if (sidecar == nullptr) continue;
            const auto found = std::find_if(
                sidecar->machine_runtime_records.begin(),
                sidecar->machine_runtime_records.end(),
                [entity_guid](const MachineRuntimePersistenceRecord& candidate) {
                    return candidate.entity_guid == entity_guid;
                });
            if (found != sidecar->machine_runtime_records.end()) {
                record = &*found;
                break;
            }
        }
        if (record == nullptr) continue;
        const MachineDefinition* definition = content_->find_machine(record->machine_id);
        if (definition == nullptr ||
            definition->offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland) {
            continue;
        }
        batch_ticks = std::min(batch_ticks, batch_ticks_for(*definition));
    }
    return batch_ticks;
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
