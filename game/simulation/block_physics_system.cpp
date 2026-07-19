// Deterministic game-owned terrain physics implementation.

#include "game/simulation/block_physics_system.h"

#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace snt::game {
namespace {

constexpr std::array<std::array<int32_t, 3>, 7> kNeighborDeltas = {{
    {{0, 0, 0}},
    {{1, 0, 0}}, {{-1, 0, 0}},
    {{0, 1, 0}}, {{0, -1, 0}},
    {{0, 0, 1}}, {{0, 0, -1}},
}};

struct ResolvedCell {
    snt::voxel::VoxelChunk* chunk = nullptr;
    snt::voxel::TerrainCell* cell = nullptr;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    int32_t local_z = 0;
};

int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

int32_t local_coordinate(int32_t value, int32_t chunk_coordinate,
                         int32_t chunk_size) noexcept {
    return static_cast<int32_t>(static_cast<int64_t>(value) -
                                static_cast<int64_t>(chunk_coordinate) * chunk_size);
}

bool offset_coordinate(int32_t value, int64_t offset, int32_t& out) noexcept {
    const int64_t result = static_cast<int64_t>(value) + offset;
    if (result < std::numeric_limits<int32_t>::min() ||
        result > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    out = static_cast<int32_t>(result);
    return true;
}

uint64_t saturating_add(uint64_t value, uint64_t delta) noexcept {
    return value > (std::numeric_limits<uint64_t>::max)() - delta
        ? (std::numeric_limits<uint64_t>::max)()
        : value + delta;
}

uint64_t mix64(uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

uint64_t hash_dimension(std::string_view dimension_id) noexcept {
    uint64_t value = 1469598103934665603ULL;
    for (const unsigned char character : dimension_id) {
        value ^= character;
        value *= 1099511628211ULL;
    }
    return value;
}

float deterministic_roll(std::string_view dimension_id,
                         int32_t block_x,
                         int32_t block_y,
                         int32_t block_z,
                         uint64_t tick) noexcept {
    uint64_t value = hash_dimension(dimension_id);
    value = mix64(value ^ tick);
    value = mix64(value ^ static_cast<uint32_t>(block_x));
    value = mix64(value ^ static_cast<uint32_t>(block_y));
    value = mix64(value ^ static_cast<uint32_t>(block_z));
    return static_cast<float>(value >> 40U) * (1.0f / 16777216.0f);
}

ResolvedCell resolve_cell(snt::voxel::ChunkRegistry& chunks,
                          std::string_view dimension_id,
                          int32_t block_x,
                          int32_t block_y,
                          int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    ResolvedCell result;
    result.chunk_x = floor_divide(block_x, kChunkSize);
    result.chunk_y = floor_divide(block_y, kChunkSize);
    result.chunk_z = floor_divide(block_z, kChunkSize);
    result.local_x = local_coordinate(block_x, result.chunk_x, kChunkSize);
    result.local_y = local_coordinate(block_y, result.chunk_y, kChunkSize);
    result.local_z = local_coordinate(block_z, result.chunk_z, kChunkSize);
    result.chunk = chunks.get_chunk(std::string(dimension_id), result.chunk_x,
                                    result.chunk_y, result.chunk_z);
    if (result.chunk == nullptr ||
        !result.chunk->terrain.is_valid_cell(result.local_x, result.local_y, result.local_z)) {
        result.chunk = nullptr;
        return result;
    }
    result.cell = &result.chunk->terrain.cell_at(result.local_x, result.local_y, result.local_z);
    return result;
}

const snt::voxel::TerrainCell* resolve_const_cell(const snt::voxel::ChunkRegistry& chunks,
                                                   std::string_view dimension_id,
                                                   int32_t block_x,
                                                   int32_t block_y,
                                                   int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int32_t chunk_x = floor_divide(block_x, kChunkSize);
    const int32_t chunk_y = floor_divide(block_y, kChunkSize);
    const int32_t chunk_z = floor_divide(block_z, kChunkSize);
    const int32_t local_x = local_coordinate(block_x, chunk_x, kChunkSize);
    const int32_t local_y = local_coordinate(block_y, chunk_y, kChunkSize);
    const int32_t local_z = local_coordinate(block_z, chunk_z, kChunkSize);
    const snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        std::string(dimension_id), chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr || !chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
        return nullptr;
    }
    return &chunk->terrain.cell_at(local_x, local_y, local_z);
}

bool is_empty_cell(const snt::voxel::TerrainCell& cell,
                   snt::voxel::TerrainMaterialId air_material) noexcept {
    return cell.material == air_material && !cell.has_fluid();
}

bool allows_chain_depth(int configured_limit, uint32_t chain_depth) noexcept {
    return configured_limit >= 0 && chain_depth <= static_cast<uint32_t>(configured_limit);
}

}  // namespace

GameBlockPhysicsSystem::GameBlockPhysicsSystem(
    snt::voxel::ChunkRegistry& chunks,
    const WorldGenConfigSnapshot& worldgen_config,
    const GameplayConfig& gameplay_config) noexcept
    : chunks_(&chunks),
      worldgen_config_(&worldgen_config),
      gameplay_config_(&gameplay_config) {}

void GameBlockPhysicsSystem::schedule_after_terrain_mutation(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    uint64_t source_tick) {
    if (dimension_id.empty()) return;
    schedule_gravity_checks(dimension_id, block_x, block_y, block_z, source_tick, 0);
    schedule_collapse_checks(dimension_id, block_x, block_y, block_z, source_tick, 0);
}

void GameBlockPhysicsSystem::tick(uint64_t current_tick) {
    size_t scanned = pending_.size();
    uint32_t processed = 0;
    while (!pending_.empty() && scanned > 0 && processed < kMaxChecksPerTick) {
        PendingCheck check = std::move(pending_.front());
        pending_.pop_front();
        --scanned;
        if (check.target_tick > current_tick) {
            pending_.push_back(std::move(check));
            continue;
        }

        const bool changed = check.kind == CheckKind::kGravityFall
            ? process_gravity_fall(check)
            : process_collapse(check, current_tick);
        if (changed && check.chain_depth < (std::numeric_limits<uint32_t>::max)()) {
            const uint32_t next_depth = check.chain_depth + 1;
            schedule_gravity_checks(check.dimension_id, check.block_x, check.block_y, check.block_z,
                                    current_tick, next_depth);
            schedule_collapse_checks(check.dimension_id, check.block_x, check.block_y, check.block_z,
                                     current_tick, next_depth);
        }
        ++processed;
    }
}

void GameBlockPhysicsSystem::schedule_gravity_checks(std::string_view dimension_id,
                                                      int32_t block_x,
                                                      int32_t block_y,
                                                      int32_t block_z,
                                                      uint64_t source_tick,
                                                      uint32_t chain_depth) {
    if (gameplay_config_ == nullptr ||
        !gameplay_config_->is_gravity_fall_enabled(std::string(dimension_id)) ||
        !allows_chain_depth(gameplay_config_->get_max_gravity_fall_chain(
                                std::string(dimension_id)),
                            chain_depth)) {
        return;
    }

    for (size_t index = 0; index < kNeighborDeltas.size(); ++index) {
        int32_t check_x = 0;
        int32_t check_y = 0;
        int32_t check_z = 0;
        if (!offset_coordinate(block_x, kNeighborDeltas[index][0], check_x) ||
            !offset_coordinate(block_y, kNeighborDeltas[index][1], check_y) ||
            !offset_coordinate(block_z, kNeighborDeltas[index][2], check_z)) {
            continue;
        }
        pending_.push_back({
            .dimension_id = std::string(dimension_id),
            .block_x = check_x,
            .block_y = check_y,
            .block_z = check_z,
            .target_tick = saturating_add(source_tick, index == 0 ? 1 : 2),
            .kind = CheckKind::kGravityFall,
            .chain_depth = chain_depth,
        });
    }
}

void GameBlockPhysicsSystem::schedule_collapse_checks(std::string_view dimension_id,
                                                       int32_t block_x,
                                                       int32_t block_y,
                                                       int32_t block_z,
                                                       uint64_t source_tick,
                                                       uint32_t chain_depth) {
    if (gameplay_config_ == nullptr ||
        !gameplay_config_->is_collapse_enabled(std::string(dimension_id)) ||
        !allows_chain_depth(gameplay_config_->get_max_collapse_chain(std::string(dimension_id)),
                            chain_depth)) {
        return;
    }

    for (size_t index = 0; index < kNeighborDeltas.size(); ++index) {
        int32_t check_x = 0;
        int32_t check_y = 0;
        int32_t check_z = 0;
        if (!offset_coordinate(block_x, kNeighborDeltas[index][0], check_x) ||
            !offset_coordinate(block_y, kNeighborDeltas[index][1], check_y) ||
            !offset_coordinate(block_z, kNeighborDeltas[index][2], check_z)) {
            continue;
        }
        pending_.push_back({
            .dimension_id = std::string(dimension_id),
            .block_x = check_x,
            .block_y = check_y,
            .block_z = check_z,
            .target_tick = saturating_add(source_tick, 2 + index),
            .kind = CheckKind::kCollapse,
            .chain_depth = chain_depth,
        });
    }
}

bool GameBlockPhysicsSystem::process_gravity_fall(const PendingCheck& check) {
    if (chunks_ == nullptr || gameplay_config_ == nullptr || worldgen_config_ == nullptr ||
        !gameplay_config_->is_gravity_fall_enabled(check.dimension_id)) {
        return false;
    }

    ResolvedCell source = resolve_cell(*chunks_, check.dimension_id,
                                       check.block_x, check.block_y, check.block_z);
    if (source.cell == nullptr || !source.cell->is_gravity_fall()) return false;

    const GravityStep gravity = compute_gravity_step(check.dimension_id,
                                                     check.block_x, check.block_y, check.block_z);
    if (gravity.dx == 0 && gravity.dy == 0 && gravity.dz == 0) return false;

    int32_t destination_x = 0;
    int32_t destination_y = 0;
    int32_t destination_z = 0;
    if (!offset_coordinate(check.block_x, gravity.dx, destination_x) ||
        !offset_coordinate(check.block_y, gravity.dy, destination_y) ||
        !offset_coordinate(check.block_z, gravity.dz, destination_z)) {
        return false;
    }
    ResolvedCell destination = resolve_cell(*chunks_, check.dimension_id,
                                            destination_x, destination_y, destination_z);
    if (destination.cell == nullptr ||
        !is_empty_cell(*destination.cell, worldgen_config_->roles.air)) {
        return false;
    }

    const snt::voxel::TerrainCell source_before = *source.cell;
    const snt::voxel::TerrainCell destination_before = *destination.cell;
    *source.cell = {.material = worldgen_config_->roles.air};
    *destination.cell = {
        .material = source_before.material,
        .flags = source_before.flags,
    };
    emit_terrain_change(check.dimension_id, check.block_x, check.block_y, check.block_z,
                        source_before, *source.cell);
    emit_terrain_change(check.dimension_id, destination_x, destination_y, destination_z,
                        destination_before, *destination.cell);
    return true;
}

bool GameBlockPhysicsSystem::process_collapse(const PendingCheck& check,
                                              uint64_t current_tick) {
    if (chunks_ == nullptr || gameplay_config_ == nullptr || worldgen_config_ == nullptr ||
        !gameplay_config_->is_collapse_enabled(check.dimension_id)) {
        return false;
    }

    ResolvedCell source = resolve_cell(*chunks_, check.dimension_id,
                                       check.block_x, check.block_y, check.block_z);
    if (source.cell == nullptr || !source.cell->is_collapse_risk()) return false;
    if (has_support_beam_nearby(check.dimension_id, check.block_x, check.block_y, check.block_z,
                                gameplay_config_->get_support_beam_radius(check.dimension_id))) {
        return false;
    }

    const GravityStep gravity = compute_gravity_step(check.dimension_id,
                                                     check.block_x, check.block_y, check.block_z);
    if (gravity.dx == 0 && gravity.dy == 0 && gravity.dz == 0) return false;

    int32_t below_x = 0;
    int32_t below_y = 0;
    int32_t below_z = 0;
    if (!offset_coordinate(check.block_x, gravity.dx, below_x) ||
        !offset_coordinate(check.block_y, gravity.dy, below_y) ||
        !offset_coordinate(check.block_z, gravity.dz, below_z)) {
        return false;
    }
    const snt::voxel::TerrainCell* below = resolve_const_cell(
        *chunks_, check.dimension_id, below_x, below_y, below_z);
    if (below != nullptr && below->is_solid()) return false;

    int32_t destination_x = 0;
    int32_t destination_y = 0;
    int32_t destination_z = 0;
    bool destination_found = false;
    for (int32_t distance = 1; distance <= kMaxCollapseSettleDistance; ++distance) {
        int32_t candidate_x = 0;
        int32_t candidate_y = 0;
        int32_t candidate_z = 0;
        if (!offset_coordinate(check.block_x, static_cast<int64_t>(gravity.dx) * distance,
                               candidate_x) ||
            !offset_coordinate(check.block_y, static_cast<int64_t>(gravity.dy) * distance,
                               candidate_y) ||
            !offset_coordinate(check.block_z, static_cast<int64_t>(gravity.dz) * distance,
                               candidate_z)) {
            break;
        }
        const snt::voxel::TerrainCell* cell = resolve_const_cell(
            *chunks_, check.dimension_id, candidate_x, candidate_y, candidate_z);
        if (cell == nullptr) break;
        if (is_empty_cell(*cell, worldgen_config_->roles.air)) {
            destination_x = candidate_x;
            destination_y = candidate_y;
            destination_z = candidate_z;
            destination_found = true;
            continue;
        }
        break;
    }
    if (!destination_found) return false;

    const TerrainMaterialDef* material = worldgen_config_->find_material(source.cell->material);
    const float base_chance = material != nullptr ? material->collapse_chance : 0.3f;
    const float chance = std::clamp(
        base_chance * gameplay_config_->get_collapse_chance_multiplier(check.dimension_id),
        0.0f, 1.0f);
    // A saturated multiplier is a gameplay guarantee, not a sampled edge
    // case. Avoid allowing a floating-point roll rounded to 1.0 to reject a
    // configured 100% collapse chance.
    if (chance < 1.0f &&
        deterministic_roll(check.dimension_id, check.block_x, check.block_y, check.block_z,
                           current_tick) >= chance) {
        return false;
    }

    ResolvedCell destination = resolve_cell(*chunks_, check.dimension_id,
                                            destination_x, destination_y, destination_z);
    if (destination.cell == nullptr ||
        !is_empty_cell(*destination.cell, worldgen_config_->roles.air)) {
        return false;
    }

    const snt::voxel::TerrainCell source_before = *source.cell;
    const snt::voxel::TerrainCell destination_before = *destination.cell;
    *source.cell = {.material = worldgen_config_->roles.air};
    *destination.cell = {
        .material = source_before.material,
        .flags = source_before.flags,
    };
    emit_terrain_change(check.dimension_id, check.block_x, check.block_y, check.block_z,
                        source_before, *source.cell);
    emit_terrain_change(check.dimension_id, destination_x, destination_y, destination_z,
                        destination_before, *destination.cell);
    return true;
}

GameBlockPhysicsSystem::GravityStep GameBlockPhysicsSystem::compute_gravity_step(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z) const noexcept {
    GravityStep step;
    if (worldgen_config_ == nullptr) return step;
    const PlanetConfig* planet = worldgen_config_->find_planet_config(std::string(dimension_id));
    if (planet == nullptr || !planet->is_planet()) return step;

    const float dx = static_cast<float>(block_x) - planet->center_x;
    const float dy = static_cast<float>(block_y) - planet->center_y;
    const float dz = static_cast<float>(block_z) - planet->center_z;
    const float abs_x = std::fabs(dx);
    const float abs_y = std::fabs(dy);
    const float abs_z = std::fabs(dz);
    step = {};
    if (abs_x >= abs_y && abs_x >= abs_z) {
        step.dx = dx > 0.0f ? -1 : 1;
    } else if (abs_y >= abs_x && abs_y >= abs_z) {
        step.dy = dy > 0.0f ? -1 : 1;
    } else {
        step.dz = dz > 0.0f ? -1 : 1;
    }
    return step;
}

bool GameBlockPhysicsSystem::has_support_beam_nearby(std::string_view dimension_id,
                                                      int32_t block_x,
                                                      int32_t block_y,
                                                      int32_t block_z,
                                                      int32_t radius) const {
    if (chunks_ == nullptr || radius <= 0) return false;
    for (int32_t offset_y = -radius; offset_y <= radius; ++offset_y) {
        for (int32_t offset_z = -radius; offset_z <= radius; ++offset_z) {
            for (int32_t offset_x = -radius; offset_x <= radius; ++offset_x) {
                if (offset_x == 0 && offset_y == 0 && offset_z == 0) continue;
                if (std::abs(static_cast<int64_t>(offset_x)) +
                        std::abs(static_cast<int64_t>(offset_y)) +
                        std::abs(static_cast<int64_t>(offset_z)) >
                    radius) {
                    continue;
                }
                int32_t candidate_x = 0;
                int32_t candidate_y = 0;
                int32_t candidate_z = 0;
                if (!offset_coordinate(block_x, offset_x, candidate_x) ||
                    !offset_coordinate(block_y, offset_y, candidate_y) ||
                    !offset_coordinate(block_z, offset_z, candidate_z)) {
                    continue;
                }
                const snt::voxel::TerrainCell* cell = resolve_const_cell(
                    *chunks_, dimension_id, candidate_x, candidate_y, candidate_z);
                if (cell != nullptr && cell->is_support_beam()) return true;
            }
        }
    }
    return false;
}

void GameBlockPhysicsSystem::emit_terrain_change(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    const snt::voxel::TerrainCell& previous,
    const snt::voxel::TerrainCell& current) const {
    if (mutation_sink_ == nullptr) return;
    mutation_sink_->on_block_physics_terrain_changed({
        .dimension_id = std::string(dimension_id),
        .block_x = block_x,
        .block_y = block_y,
        .block_z = block_z,
        .previous_material = previous.material,
        .previous_flags = previous.flags,
        .current_material = current.material,
        .current_flags = current.flags,
    });
}

}  // namespace snt::game
