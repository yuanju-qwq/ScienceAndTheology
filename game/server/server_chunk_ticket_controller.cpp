// Dedicated-server terrain ticket policy implementation.

#define SNT_LOG_CHANNEL "game.server_chunk_tickets"
#include "game/server/server_chunk_ticket_controller.h"

#include "core/error.h"
#include "game/simulation/science_and_theology_simulation_session.h"
#include "voxel/data/voxel_chunk.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

struct ChunkKeyLess {
    [[nodiscard]] bool operator()(const ChunkKey& left, const ChunkKey& right) const noexcept {
        if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
        if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
        if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
        return left.chunk_z < right.chunk_z;
    }
};

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] int64_t floor_divide(int64_t value, int64_t divisor) noexcept {
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] snt::core::Expected<void> append_position_tickets(
    std::set<ChunkKey, ChunkKeyLess>& tickets,
    const GamePlayerWorldPosition& center,
    uint32_t horizontal_radius_blocks,
    uint32_t vertical_radius_blocks,
    uint32_t maximum_ticket_count) {
    if (center.dimension_id.empty()) return {};
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t horizontal_radius = static_cast<int64_t>(horizontal_radius_blocks);
    const int64_t vertical_radius = static_cast<int64_t>(vertical_radius_blocks);
    const int64_t min_x = floor_divide(static_cast<int64_t>(center.position.x) -
                                       horizontal_radius, kChunkSize);
    const int64_t max_x = floor_divide(static_cast<int64_t>(center.position.x) +
                                       horizontal_radius, kChunkSize);
    const int64_t min_y = floor_divide(static_cast<int64_t>(center.position.y) -
                                       vertical_radius, kChunkSize);
    const int64_t max_y = floor_divide(static_cast<int64_t>(center.position.y) +
                                       vertical_radius, kChunkSize);
    const int64_t min_z = floor_divide(static_cast<int64_t>(center.position.z) -
                                       horizontal_radius, kChunkSize);
    const int64_t max_z = floor_divide(static_cast<int64_t>(center.position.z) +
                                       horizontal_radius, kChunkSize);
    if (min_x < std::numeric_limits<int>::min() || max_x > std::numeric_limits<int>::max() ||
        min_y < std::numeric_limits<int>::min() || max_y > std::numeric_limits<int>::max() ||
        min_z < std::numeric_limits<int>::min() || max_z > std::numeric_limits<int>::max()) {
        return invalid_argument("Chunk ticket AOI exceeds supported chunk-coordinate range");
    }

    for (int64_t chunk_x = min_x; chunk_x <= max_x; ++chunk_x) {
        for (int64_t chunk_y = min_y; chunk_y <= max_y; ++chunk_y) {
            for (int64_t chunk_z = min_z; chunk_z <= max_z; ++chunk_z) {
                tickets.emplace(center.dimension_id, static_cast<int>(chunk_x),
                                static_cast<int>(chunk_y), static_cast<int>(chunk_z));
                if (tickets.size() > maximum_ticket_count) {
                    return invalid_argument(
                        "Chunk ticket AOI exceeds the configured maximum ticket count");
                }
            }
        }
    }
    return {};
}

}  // namespace

ServerChunkTicketController::ServerChunkTicketController(
    ScienceAndTheologySimulationSession& simulation_session,
    ServerChunkTicketConfig config) noexcept
    : simulation_session_(&simulation_session), config_(std::move(config)) {}

snt::core::Expected<void> ServerChunkTicketController::reconcile(
    uint64_t current_tick,
    std::span<const GamePlayerWorldPosition> active_player_positions) {
    if (simulation_session_ == nullptr) {
        return invalid_argument("Chunk ticket controller is unavailable");
    }
    if (config_.permanent_spawn.dimension_id.empty()) {
        return invalid_argument("Chunk ticket permanent spawn dimension must not be empty");
    }
    if (config_.max_ticketed_chunks == 0) {
        return invalid_argument("Chunk ticket maximum must be positive");
    }

    std::set<ChunkKey, ChunkKeyLess> tickets;
    if (auto result = append_position_tickets(
            tickets, config_.permanent_spawn, config_.horizontal_aoi_radius_blocks,
            config_.vertical_aoi_radius_blocks, config_.max_ticketed_chunks);
        !result) {
        return result.error();
    }
    for (const GamePlayerWorldPosition& position : active_player_positions) {
        if (auto result = append_position_tickets(
                tickets, position, config_.horizontal_aoi_radius_blocks,
                config_.vertical_aoi_radius_blocks, config_.max_ticketed_chunks);
            !result) {
            return result.error();
        }
    }

    std::vector<ChunkKey> requested(tickets.begin(), tickets.end());
    auto reconciled = simulation_session_->reconcile_chunk_tickets(current_tick, requested);
    if (!reconciled) {
        auto error = reconciled.error();
        error.with_context("ServerChunkTicketController::reconcile");
        return error;
    }
    return {};
}

}  // namespace snt::game
