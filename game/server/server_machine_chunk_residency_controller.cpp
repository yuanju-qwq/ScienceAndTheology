// Dedicated-server machine ECS residency implementation.

#define SNT_LOG_CHANNEL "game.server_machine_residency"
#include "game/server/server_machine_chunk_residency_controller.h"

#include "core/error.h"
#include "core/log.h"
#include "game/simulation/science_and_theology_simulation_session.h"
#include "game/world/game_chunk.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

enum class MachineChunkResidencyTarget : uint8_t {
    kMaterialized = 0,
    kOffline = 1,
};

struct PlannedMachineChunkTransition {
    ChunkKey chunk_key;
    MachineChunkResidencyTarget target = MachineChunkResidencyTarget::kMaterialized;
    size_t machine_count = 0;
};

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] int64_t distance_to_range(int64_t value, int64_t minimum,
                                         int64_t maximum) noexcept {
    if (value < minimum) return minimum - value;
    if (value > maximum) return value - maximum;
    return 0;
}

[[nodiscard]] bool chunk_key_less(const ChunkKey& left, const ChunkKey& right) noexcept {
    if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
    if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
    if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
    return left.chunk_z < right.chunk_z;
}

[[nodiscard]] bool is_chunk_ticketed(
    const ChunkKey& chunk_key,
    const GamePlayerWorldPosition& interest_center,
    uint32_t horizontal_aoi_radius_blocks,
    uint32_t vertical_aoi_radius_blocks) noexcept {
    if (chunk_key.dimension_id != interest_center.dimension_id) return false;

    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t minimum_x = static_cast<int64_t>(chunk_key.chunk_x) * kChunkSize;
    const int64_t minimum_y = static_cast<int64_t>(chunk_key.chunk_y) * kChunkSize;
    const int64_t minimum_z = static_cast<int64_t>(chunk_key.chunk_z) * kChunkSize;
    const int64_t maximum_x = minimum_x + kChunkSize - 1;
    const int64_t maximum_y = minimum_y + kChunkSize - 1;
    const int64_t maximum_z = minimum_z + kChunkSize - 1;

    return distance_to_range(interest_center.position.x, minimum_x, maximum_x) <=
               static_cast<int64_t>(horizontal_aoi_radius_blocks) &&
           distance_to_range(interest_center.position.y, minimum_y, maximum_y) <=
               static_cast<int64_t>(vertical_aoi_radius_blocks) &&
           distance_to_range(interest_center.position.z, minimum_z, maximum_z) <=
               static_cast<int64_t>(horizontal_aoi_radius_blocks);
}

}  // namespace

ServerMachineChunkResidencyController::ServerMachineChunkResidencyController(
    ScienceAndTheologySimulationSession& simulation_session,
    ServerMachineChunkResidencyConfig config) noexcept
    : simulation_session_(&simulation_session), config_(std::move(config)) {}

snt::core::Expected<void> ServerMachineChunkResidencyController::reconcile(
    uint64_t current_tick,
    std::span<const GamePlayerWorldPosition> active_player_positions) {
    if (simulation_session_ == nullptr) {
        return invalid_argument("Machine chunk residency controller is unavailable");
    }
    if (config_.permanent_spawn.dimension_id.empty()) {
        return invalid_argument("Machine chunk residency permanent spawn dimension must not be empty");
    }

    std::vector<PlannedMachineChunkTransition> transitions;
    simulation_session_->world_sidecars().for_each(
        [&](const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
            if (sidecar.machine_runtime_records.empty()) return;

            bool ticketed = is_chunk_ticketed(
                chunk_key, config_.permanent_spawn,
                config_.horizontal_aoi_radius_blocks,
                config_.vertical_aoi_radius_blocks);
            for (const GamePlayerWorldPosition& position : active_player_positions) {
                if (ticketed) break;
                ticketed = is_chunk_ticketed(
                    chunk_key, position,
                    config_.horizontal_aoi_radius_blocks,
                    config_.vertical_aoi_radius_blocks);
            }

            size_t loaded_machine_count = 0;
            size_t non_loaded_machine_count = 0;
            for (const MachineRuntimePersistenceRecord& record :
                 sidecar.machine_runtime_records) {
                if (record.residency == MachineRuntimeResidency::kLoaded) {
                    ++loaded_machine_count;
                } else {
                    ++non_loaded_machine_count;
                }
            }

            if (ticketed && non_loaded_machine_count != 0) {
                transitions.push_back({
                    .chunk_key = chunk_key,
                    .target = MachineChunkResidencyTarget::kMaterialized,
                    .machine_count = non_loaded_machine_count,
                });
            } else if (!ticketed && loaded_machine_count != 0) {
                transitions.push_back({
                    .chunk_key = chunk_key,
                    .target = MachineChunkResidencyTarget::kOffline,
                    .machine_count = loaded_machine_count,
                });
            }
        });

    std::sort(transitions.begin(), transitions.end(),
              [](const PlannedMachineChunkTransition& left,
                 const PlannedMachineChunkTransition& right) {
                  return chunk_key_less(left.chunk_key, right.chunk_key);
              });

    size_t materialized_chunks = 0;
    size_t materialized_machines = 0;
    size_t dematerialized_chunks = 0;
    size_t dematerialized_machines = 0;
    for (const PlannedMachineChunkTransition& transition : transitions) {
        if (transition.target == MachineChunkResidencyTarget::kMaterialized) {
            if (auto result = simulation_session_->materialize_chunk_machines(
                    transition.chunk_key, current_tick);
                !result) {
                auto error = result.error();
                error.with_context(
                    "ServerMachineChunkResidencyController::reconcile(materialize)");
                return error;
            }
            ++materialized_chunks;
            materialized_machines += transition.machine_count;
            continue;
        }

        if (auto result = simulation_session_->dematerialize_chunk_machines(
                transition.chunk_key, current_tick);
            !result) {
            auto error = result.error();
            error.with_context(
                "ServerMachineChunkResidencyController::reconcile(dematerialize)");
            return error;
        }
        ++dematerialized_chunks;
        dematerialized_machines += transition.machine_count;
    }

    if (!transitions.empty()) {
        SNT_LOG_INFO(
            "Reconciled machine ECS residency at tick %llu: materialized=%zu chunk(s)/%zu machine(s), offline=%zu chunk(s)/%zu machine(s), player_tickets=%zu",
            static_cast<unsigned long long>(current_tick), materialized_chunks,
            materialized_machines, dematerialized_chunks, dematerialized_machines,
            active_player_positions.size());
    }
    return {};
}

}  // namespace snt::game
