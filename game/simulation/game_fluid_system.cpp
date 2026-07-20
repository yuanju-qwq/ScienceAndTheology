// Game-owned hybrid voxel-fluid simulation implementation.

#define SNT_LOG_CHANNEL "game.fluid"
#include "game/simulation/game_fluid_system.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace snt::game {
namespace {

constexpr std::array<std::array<int32_t, 3>, 6> kNeighborOffsets = {{
    {{1, 0, 0}}, {{-1, 0, 0}},
    {{0, 1, 0}}, {{0, -1, 0}},
    {{0, 0, 1}}, {{0, 0, -1}},
}};

bool is_valid_fluid_state(const snt::voxel::TerrainCell& cell) noexcept {
    return cell.has_fluid() && !cell.is_solid() &&
           cell.fluid_type != snt::voxel::kInvalidCellFluidId;
}

}  // namespace

GameFluidSystem::GameFluidSystem(snt::voxel::ChunkRegistry& chunks,
                                 const WorldGenConfigSnapshot& worldgen_config,
                                 GameFluidSystemConfig config)
    : chunks_(&chunks),
      worldgen_config_(&worldgen_config),
      config_(config) {
    config_.max_sparse_cells_per_tick =
        std::max<uint32_t>(1, config_.max_sparse_cells_per_tick);
    config_.dense_activation_cells_per_chunk =
        std::max<uint32_t>(1, config_.dense_activation_cells_per_chunk);
    config_.max_dense_chunks_per_tick =
        std::max<uint32_t>(1, config_.max_dense_chunks_per_tick);
    config_.lattice_boltzmann_activation_cells_per_chunk = std::max(
        config_.dense_activation_cells_per_chunk,
        config_.lattice_boltzmann_activation_cells_per_chunk);
    config_.lattice_boltzmann_min_cell_mass = std::clamp<int16_t>(
        config_.lattice_boltzmann_min_cell_mass, 1,
        snt::voxel::kCellFluidCapacity);
    config_.lattice_boltzmann_gravity_impulse = std::clamp<int16_t>(
        config_.lattice_boltzmann_gravity_impulse,
        static_cast<int16_t>(-kFluidLbmVelocityScale / 4),
        static_cast<int16_t>(kFluidLbmVelocityScale / 4));
}

bool GameFluidSystem::ChunkKeyLess::operator()(
    const snt::voxel::ChunkKey& left,
    const snt::voxel::ChunkKey& right) const noexcept {
    if (left.dimension_id != right.dimension_id) {
        return left.dimension_id < right.dimension_id;
    }
    if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
    if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
    return left.chunk_z < right.chunk_z;
}

bool GameFluidSystem::FluidCellKeyLess::operator()(
    const FluidCellKey& left,
    const FluidCellKey& right) const noexcept {
    if (left.dimension_id != right.dimension_id) {
        return left.dimension_id < right.dimension_id;
    }
    if (left.block_x != right.block_x) return left.block_x < right.block_x;
    if (left.block_y != right.block_y) return left.block_y < right.block_y;
    return left.block_z < right.block_z;
}

int32_t GameFluidSystem::floor_divide(int32_t value, int32_t divisor) noexcept {
    int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

bool GameFluidSystem::offset_coordinate(int32_t value, int32_t offset,
                                        int32_t& out) noexcept {
    const int64_t candidate = static_cast<int64_t>(value) + offset;
    if (candidate < std::numeric_limits<int32_t>::min() ||
        candidate > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    out = static_cast<int32_t>(candidate);
    return true;
}

bool GameFluidSystem::same_terrain_cell(const snt::voxel::TerrainCell& left,
                                        const snt::voxel::TerrainCell& right) noexcept {
    return left.material == right.material &&
           left.flags == right.flags &&
           left.fluid_type == right.fluid_type &&
           left.fluid_mass == right.fluid_mass &&
           left.fluid_temperature == right.fluid_temperature &&
           left.fluid_is_gas == right.fluid_is_gas;
}

bool GameFluidSystem::same_fluid_state(const DenseCellState& left,
                                       const DenseCellState& right) noexcept {
    return left.fluid_type == right.fluid_type &&
           left.mass == right.mass &&
           left.temperature == right.temperature &&
           left.is_gas == right.is_gas;
}

snt::voxel::ChunkKey GameFluidSystem::chunk_key_for(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z) const {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return {
        std::string(dimension_id),
        floor_divide(block_x, kChunkSize),
        floor_divide(block_y, kChunkSize),
        floor_divide(block_z, kChunkSize),
    };
}

GameFluidSystem::ResolvedCell GameFluidSystem::resolve_cell(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z) {
    ResolvedCell result;
    if (chunks_ == nullptr || dimension_id.empty()) return result;

    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    result.chunk_key = chunk_key_for(dimension_id, block_x, block_y, block_z);
    result.local_x = static_cast<int32_t>(
        static_cast<int64_t>(block_x) -
        static_cast<int64_t>(result.chunk_key.chunk_x) * kChunkSize);
    result.local_y = static_cast<int32_t>(
        static_cast<int64_t>(block_y) -
        static_cast<int64_t>(result.chunk_key.chunk_y) * kChunkSize);
    result.local_z = static_cast<int32_t>(
        static_cast<int64_t>(block_z) -
        static_cast<int64_t>(result.chunk_key.chunk_z) * kChunkSize);
    result.chunk = chunks_->get_chunk(result.chunk_key.dimension_id,
                                      result.chunk_key.chunk_x,
                                      result.chunk_key.chunk_y,
                                      result.chunk_key.chunk_z);
    if (result.chunk == nullptr ||
        !result.chunk->terrain.is_valid_cell(result.local_x, result.local_y,
                                              result.local_z)) {
        result.chunk = nullptr;
        return result;
    }
    result.cell = &result.chunk->terrain.cell_at(result.local_x, result.local_y,
                                                  result.local_z);
    return result;
}

GameFluidSystem::GravityStep GameFluidSystem::gravity_step(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z) const noexcept {
    GravityStep step;
    if (worldgen_config_ == nullptr) return step;

    const PlanetConfig* planet = worldgen_config_->find_planet_config(
        std::string(dimension_id));
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

bool GameFluidSystem::chunk_is_dense(const snt::voxel::ChunkKey& key) const noexcept {
    return dense_chunks_.contains(key);
}

bool GameFluidSystem::chunk_has_active_cells(const snt::voxel::ChunkKey& key) const {
    for (const FluidCellKey& cell : active_cells_) {
        if (chunk_key_for(cell.dimension_id, cell.block_x, cell.block_y,
                          cell.block_z) == key) {
            return true;
        }
    }
    return false;
}

bool GameFluidSystem::can_use_lattice_boltzmann(
    const snt::voxel::ChunkKey& key) const {
    if (!config_.enable_lattice_boltzmann || chunks_ == nullptr) return false;
    const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr || chunk->terrain.size_x < 3 ||
        chunk->terrain.size_y < 3 || chunk->terrain.size_z < 3) {
        return false;
    }

    snt::voxel::CellFluidId fluid_type = snt::voxel::kInvalidCellFluidId;
    uint32_t fluid_cells = 0;
    for (int32_t local_y = 0; local_y < chunk->terrain.size_y; ++local_y) {
        for (int32_t local_z = 0; local_z < chunk->terrain.size_z; ++local_z) {
            for (int32_t local_x = 0; local_x < chunk->terrain.size_x; ++local_x) {
                const snt::voxel::TerrainCell& cell = chunk->terrain.cell_at(
                    local_x, local_y, local_z);
                if (!is_valid_fluid_state(cell)) continue;
                // The local D3Q7 state intentionally bounces at chunk edges.
                // Hand off to CA before a flow can require a loaded neighbor.
                if (local_x == 0 || local_y == 0 || local_z == 0 ||
                    local_x == chunk->terrain.size_x - 1 ||
                    local_y == chunk->terrain.size_y - 1 ||
                    local_z == chunk->terrain.size_z - 1 ||
                    cell.fluid_is_gas ||
                    cell.fluid_mass < config_.lattice_boltzmann_min_cell_mass) {
                    return false;
                }
                if (fluid_type == snt::voxel::kInvalidCellFluidId) {
                    fluid_type = cell.fluid_type;
                } else if (fluid_type != cell.fluid_type) {
                    return false;
                }
                if (fluid_cells != std::numeric_limits<uint32_t>::max()) {
                    ++fluid_cells;
                }
            }
        }
    }
    return fluid_type != snt::voxel::kInvalidCellFluidId &&
           fluid_cells >= config_.lattice_boltzmann_activation_cells_per_chunk;
}

FluidSimulationLayer GameFluidSystem::dense_layer_for(
    const snt::voxel::ChunkKey& key) const noexcept {
    const auto found = dense_chunk_layers_.find(key);
    return found == dense_chunk_layers_.end()
        ? FluidSimulationLayer::kDenseCellularAutomaton
        : found->second;
}

void GameFluidSystem::wake_cell(std::string_view dimension_id, int32_t block_x,
                                int32_t block_y, int32_t block_z) {
    if (dimension_id.empty()) return;
    const snt::voxel::ChunkKey chunk = chunk_key_for(dimension_id, block_x,
                                                       block_y, block_z);
    equilibrium_chunks_.erase(chunk);
    equilibrium_candidates_.insert(chunk);
    active_cells_.insert({std::string(dimension_id), block_x, block_y, block_z});
}

void GameFluidSystem::wake_fluid_neighbors(std::string_view dimension_id,
                                           int32_t block_x, int32_t block_y,
                                           int32_t block_z) {
    for (const auto& offset : kNeighborOffsets) {
        int32_t neighbor_x = 0;
        int32_t neighbor_y = 0;
        int32_t neighbor_z = 0;
        if (!offset_coordinate(block_x, offset[0], neighbor_x) ||
            !offset_coordinate(block_y, offset[1], neighbor_y) ||
            !offset_coordinate(block_z, offset[2], neighbor_z)) {
            continue;
        }
        const ResolvedCell neighbor = resolve_cell(dimension_id, neighbor_x,
                                                   neighbor_y, neighbor_z);
        if (neighbor.cell != nullptr && is_valid_fluid_state(*neighbor.cell)) {
            invalidate_lattice_boltzmann_state(neighbor.chunk_key);
            wake_cell(dimension_id, neighbor_x, neighbor_y, neighbor_z);
        }
    }
}

void GameFluidSystem::mark_chunk_touched(const snt::voxel::ChunkKey& key,
                                         FluidSimulationLayer layer) {
    if (layer != FluidSimulationLayer::kLatticeBoltzmann) {
        invalidate_lattice_boltzmann_state(key);
    }
    equilibrium_chunks_.erase(key);
    equilibrium_candidates_.insert(key);
    presentation_dirty_chunks_.insert_or_assign(key, layer);
}

void GameFluidSystem::invalidate_lattice_boltzmann_state(
    const snt::voxel::ChunkKey& key) {
    lattice_boltzmann_states_.erase(key);
}

void GameFluidSystem::emit_terrain_change(
    const FluidCellKey& key, const snt::voxel::TerrainCell& previous,
    const snt::voxel::TerrainCell& current, FluidSimulationLayer layer) {
    if (same_terrain_cell(previous, current)) return;

    const snt::voxel::ChunkKey chunk = chunk_key_for(key.dimension_id, key.block_x,
                                                       key.block_y, key.block_z);
    mark_chunk_touched(chunk, layer);
    ++last_telemetry_.terrain_mutations;
    if (mutation_sink_ == nullptr) return;

    mutation_sink_->on_fluid_terrain_changed({
        .dimension_id = key.dimension_id,
        .block_x = key.block_x,
        .block_y = key.block_y,
        .block_z = key.block_z,
        .previous_material = previous.material,
        .previous_flags = previous.flags,
        .previous_fluid_type = previous.fluid_type,
        .previous_fluid_mass = previous.fluid_mass,
        .previous_fluid_temperature = previous.fluid_temperature,
        .previous_fluid_is_gas = previous.fluid_is_gas,
        .current_material = current.material,
        .current_flags = current.flags,
        .current_fluid_type = current.fluid_type,
        .current_fluid_mass = current.fluid_mass,
        .current_fluid_temperature = current.fluid_temperature,
        .current_fluid_is_gas = current.fluid_is_gas,
        .layer = layer,
    });
}

void GameFluidSystem::initialize_loaded_chunks() {
    if (chunks_ == nullptr) return;
    for (const snt::voxel::ChunkKey& key : chunks_->all_chunk_keys()) {
        snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr) continue;
        for (int32_t local_y = 0; local_y < chunk->terrain.size_y; ++local_y) {
            for (int32_t local_z = 0; local_z < chunk->terrain.size_z; ++local_z) {
                for (int32_t local_x = 0; local_x < chunk->terrain.size_x; ++local_x) {
                    const snt::voxel::TerrainCell& cell = chunk->terrain.cell_at(
                        local_x, local_y, local_z);
                    if (!is_valid_fluid_state(cell)) continue;
                    const int64_t world_x = static_cast<int64_t>(key.chunk_x) *
                                                snt::voxel::VoxelChunk::kChunkSize + local_x;
                    const int64_t world_y = static_cast<int64_t>(key.chunk_y) *
                                                snt::voxel::VoxelChunk::kChunkSize + local_y;
                    const int64_t world_z = static_cast<int64_t>(key.chunk_z) *
                                                snt::voxel::VoxelChunk::kChunkSize + local_z;
                    if (world_x < std::numeric_limits<int32_t>::min() ||
                        world_x > std::numeric_limits<int32_t>::max() ||
                        world_y < std::numeric_limits<int32_t>::min() ||
                        world_y > std::numeric_limits<int32_t>::max() ||
                        world_z < std::numeric_limits<int32_t>::min() ||
                        world_z > std::numeric_limits<int32_t>::max()) {
                        continue;
                    }
                    wake_cell(key.dimension_id, static_cast<int32_t>(world_x),
                              static_cast<int32_t>(world_y),
                              static_cast<int32_t>(world_z));
                }
            }
        }
    }
}

void GameFluidSystem::schedule_after_terrain_mutation(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z, uint64_t source_tick) {
    (void)source_tick;
    if (dimension_id.empty()) return;
    invalidate_lattice_boltzmann_state(
        chunk_key_for(dimension_id, block_x, block_y, block_z));
    const ResolvedCell current = resolve_cell(dimension_id, block_x, block_y, block_z);
    if (current.cell != nullptr && is_valid_fluid_state(*current.cell)) {
        wake_cell(dimension_id, block_x, block_y, block_z);
    } else {
        const snt::voxel::ChunkKey chunk = chunk_key_for(dimension_id, block_x,
                                                           block_y, block_z);
        equilibrium_chunks_.erase(chunk);
        equilibrium_candidates_.insert(chunk);
    }
    wake_fluid_neighbors(dimension_id, block_x, block_y, block_z);
}

int16_t GameFluidSystem::inject_fluid(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z, snt::voxel::CellFluidId fluid_type, int16_t amount,
    int16_t temperature, bool is_gas) {
    if (fluid_type == snt::voxel::kInvalidCellFluidId || amount <= 0) return 0;
    ResolvedCell target = resolve_cell(dimension_id, block_x, block_y, block_z);
    if (target.cell == nullptr || target.cell->is_solid() ||
        (target.cell->has_fluid() &&
         (target.cell->fluid_type != fluid_type ||
          target.cell->fluid_is_gas != is_gas))) {
        return 0;
    }

    const snt::voxel::TerrainCell previous = *target.cell;
    const int16_t inserted = target.cell->insert_fluid(fluid_type, amount, is_gas);
    if (inserted <= 0) return 0;
    if (previous.fluid_mass == 0) {
        target.cell->fluid_temperature = temperature;
    } else {
        const int32_t weighted_temperature =
            static_cast<int32_t>(previous.fluid_temperature) * previous.fluid_mass +
            static_cast<int32_t>(temperature) * inserted;
        target.cell->fluid_temperature = static_cast<int16_t>(
            weighted_temperature / target.cell->fluid_mass);
    }
    const FluidCellKey key{std::string(dimension_id), block_x, block_y, block_z};
    emit_terrain_change(key, previous, *target.cell, FluidSimulationLayer::kSparse);
    wake_cell(dimension_id, block_x, block_y, block_z);
    wake_fluid_neighbors(dimension_id, block_x, block_y, block_z);
    return inserted;
}

int16_t GameFluidSystem::extract_fluid(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z, int16_t amount) {
    if (amount <= 0) return 0;
    ResolvedCell source = resolve_cell(dimension_id, block_x, block_y, block_z);
    if (source.cell == nullptr || !is_valid_fluid_state(*source.cell)) return 0;

    const snt::voxel::TerrainCell previous = *source.cell;
    const int16_t extracted = source.cell->extract_fluid(amount);
    if (extracted <= 0) return 0;
    const FluidCellKey key{std::string(dimension_id), block_x, block_y, block_z};
    emit_terrain_change(key, previous, *source.cell, FluidSimulationLayer::kSparse);
    wake_cell(dimension_id, block_x, block_y, block_z);
    wake_fluid_neighbors(dimension_id, block_x, block_y, block_z);
    return extracted;
}

void GameFluidSystem::tick(uint64_t tick_index) {
    last_telemetry_ = {};
    last_telemetry_.tick_index = tick_index;
    last_telemetry_.active_cells_before = static_cast<uint32_t>(
        std::min<size_t>(active_cells_.size(), std::numeric_limits<uint32_t>::max()));

    promote_dense_chunks();
    process_dense_chunks(tick_index);
    process_sparse_cells();
    update_equilibrium_candidates();
    flush_presentation_updates(tick_index);

    last_telemetry_.active_cells_after = static_cast<uint32_t>(
        std::min<size_t>(active_cells_.size(), std::numeric_limits<uint32_t>::max()));
    last_telemetry_.equilibrium_chunks = static_cast<uint32_t>(
        std::min<size_t>(equilibrium_chunks_.size(), std::numeric_limits<uint32_t>::max()));
    emit_low_frequency_telemetry(tick_index);
}

const FluidEquilibriumSummary* GameFluidSystem::find_equilibrium_summary(
    const snt::voxel::ChunkKey& key) const noexcept {
    const auto found = equilibrium_chunks_.find(key);
    return found == equilibrium_chunks_.end() ? nullptr : &found->second;
}

void GameFluidSystem::promote_dense_chunks() {
    std::map<snt::voxel::ChunkKey, uint32_t, ChunkKeyLess> active_counts;
    for (const FluidCellKey& key : active_cells_) {
        const snt::voxel::ChunkKey chunk = chunk_key_for(
            key.dimension_id, key.block_x, key.block_y, key.block_z);
        uint32_t& count = active_counts[chunk];
        if (count != std::numeric_limits<uint32_t>::max()) ++count;
    }
    for (const auto& [chunk, count] : active_counts) {
        if (count < config_.dense_activation_cells_per_chunk ||
            !dense_chunks_.insert(chunk).second) {
            continue;
        }
        equilibrium_chunks_.erase(chunk);
        dense_chunk_layers_.insert_or_assign(
            chunk, can_use_lattice_boltzmann(chunk)
                       ? FluidSimulationLayer::kLatticeBoltzmann
                       : FluidSimulationLayer::kDenseCellularAutomaton);
        dense_queue_.push_back(chunk);
    }
}

void GameFluidSystem::process_dense_chunks(uint64_t tick_index) {
    uint32_t processed = 0;
    while (!dense_queue_.empty() && processed < config_.max_dense_chunks_per_tick) {
        const snt::voxel::ChunkKey chunk = std::move(dense_queue_.front());
        dense_queue_.pop_front();
        if (!dense_chunks_.contains(chunk)) {
            dense_chunk_layers_.erase(chunk);
            invalidate_lattice_boltzmann_state(chunk);
            continue;
        }

        FluidSimulationLayer layer = dense_layer_for(chunk);
        if (layer == FluidSimulationLayer::kLatticeBoltzmann &&
            !can_use_lattice_boltzmann(chunk)) {
            layer = FluidSimulationLayer::kDenseCellularAutomaton;
            dense_chunk_layers_.insert_or_assign(chunk, layer);
            invalidate_lattice_boltzmann_state(chunk);
        }

        ++processed;
        ++last_telemetry_.dense_chunks_processed;
        bool changed = false;
        if (layer == FluidSimulationLayer::kLatticeBoltzmann) {
            ++last_telemetry_.lattice_boltzmann_chunks_processed;
            changed = process_lattice_boltzmann_chunk(chunk, tick_index);
        } else {
            changed = process_dense_chunk(chunk);
        }
        if (changed) {
            dense_queue_.push_back(chunk);
            continue;
        }

        dense_chunks_.erase(chunk);
        dense_chunk_layers_.erase(chunk);
        invalidate_lattice_boltzmann_state(chunk);
        if (!dense_chunk_has_cross_boundary_flow(chunk)) {
            for (auto active = active_cells_.begin(); active != active_cells_.end();) {
                if (chunk_key_for(active->dimension_id, active->block_x,
                                  active->block_y, active->block_z) == chunk) {
                    active = active_cells_.erase(active);
                } else {
                    ++active;
                }
            }
            settle_equilibrium_chunk(chunk);
        } else {
            equilibrium_candidates_.insert(chunk);
        }
    }
}

bool GameFluidSystem::process_dense_chunk(const snt::voxel::ChunkKey& key) {
    if (chunks_ == nullptr) return false;
    snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) return false;

    const int32_t size_x = chunk->terrain.size_x;
    const int32_t size_y = chunk->terrain.size_y;
    const int32_t size_z = chunk->terrain.size_z;
    if (size_x < 3 || size_y < 3 || size_z < 3) return false;

    const size_t cell_count = chunk->terrain.cells.size();
    std::vector<DenseCellState> snapshot(cell_count);
    std::vector<int32_t> outgoing(cell_count, 0);
    std::vector<int32_t> incoming(cell_count, 0);
    std::vector<int64_t> incoming_temperature(cell_count, 0);
    std::vector<snt::voxel::CellFluidId> incoming_type(
        cell_count, snt::voxel::kInvalidCellFluidId);
    std::vector<bool> incoming_is_gas(cell_count, false);
    for (size_t index = 0; index < cell_count; ++index) {
        const snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
        snapshot[index] = {
            .fluid_type = cell.fluid_type,
            .mass = cell.fluid_mass,
            .temperature = cell.fluid_temperature,
            .is_gas = cell.fluid_is_gas,
        };
    }

    const auto local_index = [chunk](int32_t x, int32_t y, int32_t z) {
        return chunk->terrain.index_of(x, y, z);
    };
    const auto inside = [size_x, size_y, size_z](int32_t x, int32_t y, int32_t z) {
        return x >= 0 && x < size_x && y >= 0 && y < size_y && z >= 0 && z < size_z;
    };
    const auto world_coordinate = [](int32_t chunk_coordinate, int32_t local) {
        return static_cast<int32_t>(static_cast<int64_t>(chunk_coordinate) *
                                    snt::voxel::VoxelChunk::kChunkSize + local);
    };

    for (int32_t local_y = 1; local_y < size_y - 1; ++local_y) {
        for (int32_t local_z = 1; local_z < size_z - 1; ++local_z) {
            for (int32_t local_x = 1; local_x < size_x - 1; ++local_x) {
                const size_t source_index = local_index(local_x, local_y, local_z);
                const snt::voxel::TerrainCell& source_cell =
                    chunk->terrain.cells[source_index];
                const DenseCellState& source = snapshot[source_index];
                ++last_telemetry_.dense_cells_processed;
                if (source_cell.is_solid() || source.mass <= 0 ||
                    source.fluid_type == snt::voxel::kInvalidCellFluidId) {
                    continue;
                }

                const int32_t world_x = world_coordinate(key.chunk_x, local_x);
                const int32_t world_y = world_coordinate(key.chunk_y, local_y);
                const int32_t world_z = world_coordinate(key.chunk_z, local_z);
                const GravityStep gravity = gravity_step(key.dimension_id, world_x,
                                                         world_y, world_z);

                const auto plan_transfer = [&](int32_t target_x, int32_t target_y,
                                                int32_t target_z, int32_t requested,
                                                bool require_lower_mass) {
                    if (!inside(target_x, target_y, target_z) || requested <= 0) return false;
                    const size_t target_index = local_index(target_x, target_y, target_z);
                    const snt::voxel::TerrainCell& target_cell =
                        chunk->terrain.cells[target_index];
                    const DenseCellState& target = snapshot[target_index];
                    if (target_cell.is_solid()) return false;
                    if (target.mass > 0 &&
                        (target.fluid_type != source.fluid_type ||
                         target.is_gas != source.is_gas)) {
                        return false;
                    }
                    if (target.mass == 0 && incoming[target_index] > 0 &&
                        (incoming_type[target_index] != source.fluid_type ||
                         incoming_is_gas[target_index] != source.is_gas)) {
                        return false;
                    }
                    const int32_t available = source.mass - outgoing[source_index];
                    const int32_t target_mass = target.mass + incoming[target_index];
                    if (available <= 0 || target_mass >= snt::voxel::kCellFluidCapacity ||
                        (require_lower_mass && available <= target_mass)) {
                        return false;
                    }
                    const int32_t capacity = snt::voxel::kCellFluidCapacity - target_mass;
                    const int32_t moved = std::min({available, capacity, requested});
                    if (moved <= 0) return false;
                    outgoing[source_index] += moved;
                    incoming[target_index] += moved;
                    incoming_temperature[target_index] +=
                        static_cast<int64_t>(source.temperature) * moved;
                    if (target.mass == 0 && incoming[target_index] == moved) {
                        incoming_type[target_index] = source.fluid_type;
                        incoming_is_gas[target_index] = source.is_gas;
                    }
                    return true;
                };

                if (source.is_gas) {
                    int32_t best_x = 0;
                    int32_t best_y = 0;
                    int32_t best_z = 0;
                    int32_t best_mass = std::numeric_limits<int32_t>::max();
                    bool found = false;
                    for (const auto& offset : kNeighborOffsets) {
                        const int32_t candidate_x = local_x + offset[0];
                        const int32_t candidate_y = local_y + offset[1];
                        const int32_t candidate_z = local_z + offset[2];
                        if (!inside(candidate_x, candidate_y, candidate_z)) continue;
                        const size_t candidate_index = local_index(candidate_x, candidate_y,
                                                                   candidate_z);
                        const DenseCellState& candidate = snapshot[candidate_index];
                        const snt::voxel::TerrainCell& candidate_cell =
                            chunk->terrain.cells[candidate_index];
                        if (candidate_cell.is_solid() ||
                            (candidate.mass > 0 &&
                             (candidate.fluid_type != source.fluid_type ||
                              candidate.is_gas != source.is_gas))) {
                            continue;
                        }
                        const int32_t mass = candidate.mass + incoming[candidate_index];
                        if (mass < best_mass) {
                            best_mass = mass;
                            best_x = candidate_x;
                            best_y = candidate_y;
                            best_z = candidate_z;
                            found = true;
                        }
                    }
                    if (found && source.mass > best_mass) {
                        static_cast<void>(plan_transfer(best_x, best_y, best_z,
                                                        (source.mass - best_mass) / 2,
                                                        true));
                    }
                    continue;
                }

                const int32_t gravity_x = local_x + gravity.dx;
                const int32_t gravity_y = local_y + gravity.dy;
                const int32_t gravity_z = local_z + gravity.dz;
                if (plan_transfer(gravity_x, gravity_y, gravity_z,
                                  source.mass, false)) {
                    continue;
                }

                int32_t best_x = 0;
                int32_t best_y = 0;
                int32_t best_z = 0;
                int32_t best_mass = std::numeric_limits<int32_t>::max();
                bool found = false;
                for (const auto& offset : kNeighborOffsets) {
                    const int32_t dot = offset[0] * gravity.dx + offset[1] * gravity.dy +
                                        offset[2] * gravity.dz;
                    if (dot != 0) continue;
                    const int32_t candidate_x = local_x + offset[0];
                    const int32_t candidate_y = local_y + offset[1];
                    const int32_t candidate_z = local_z + offset[2];
                    if (!inside(candidate_x, candidate_y, candidate_z)) continue;
                    const size_t candidate_index = local_index(candidate_x, candidate_y,
                                                               candidate_z);
                    const DenseCellState& candidate = snapshot[candidate_index];
                    const snt::voxel::TerrainCell& candidate_cell =
                        chunk->terrain.cells[candidate_index];
                    if (candidate_cell.is_solid() ||
                        (candidate.mass > 0 &&
                         (candidate.fluid_type != source.fluid_type ||
                          candidate.is_gas != source.is_gas))) {
                        continue;
                    }
                    const int32_t mass = candidate.mass + incoming[candidate_index];
                    if (mass < best_mass) {
                        best_mass = mass;
                        best_x = candidate_x;
                        best_y = candidate_y;
                        best_z = candidate_z;
                        found = true;
                    }
                }
                if (found && source.mass > best_mass) {
                    static_cast<void>(plan_transfer(best_x, best_y, best_z,
                                                    (source.mass - best_mass) / 2,
                                                    true));
                }
            }
        }
    }

    bool changed = false;
    for (int32_t local_y = 0; local_y < size_y; ++local_y) {
        for (int32_t local_z = 0; local_z < size_z; ++local_z) {
            for (int32_t local_x = 0; local_x < size_x; ++local_x) {
                const size_t index = local_index(local_x, local_y, local_z);
                if (outgoing[index] == 0 && incoming[index] == 0) continue;
                snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
                if (cell.is_solid()) continue;
                const snt::voxel::TerrainCell previous = cell;
                const DenseCellState& initial = snapshot[index];
                const int32_t final_mass = initial.mass - outgoing[index] + incoming[index];
                if (final_mass <= 0) {
                    cell.clear_fluid();
                } else {
                    const int32_t retained_mass = std::max<int32_t>(0, initial.mass - outgoing[index]);
                    const int64_t temperature_sum =
                        static_cast<int64_t>(initial.temperature) * retained_mass +
                        incoming_temperature[index];
                    cell.fluid_mass = static_cast<int16_t>(final_mass);
                    cell.fluid_type = initial.mass > 0 ? initial.fluid_type : incoming_type[index];
                    cell.fluid_is_gas = initial.mass > 0 ? initial.is_gas : incoming_is_gas[index];
                    cell.fluid_temperature = static_cast<int16_t>(
                        temperature_sum / std::max<int32_t>(1, final_mass));
                }
                if (same_terrain_cell(previous, cell)) continue;
                changed = true;
                const int64_t world_x = static_cast<int64_t>(key.chunk_x) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_x;
                const int64_t world_y = static_cast<int64_t>(key.chunk_y) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_y;
                const int64_t world_z = static_cast<int64_t>(key.chunk_z) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_z;
                if (world_x < std::numeric_limits<int32_t>::min() ||
                    world_x > std::numeric_limits<int32_t>::max() ||
                    world_y < std::numeric_limits<int32_t>::min() ||
                    world_y > std::numeric_limits<int32_t>::max() ||
                    world_z < std::numeric_limits<int32_t>::min() ||
                    world_z > std::numeric_limits<int32_t>::max()) {
                    continue;
                }
                const FluidCellKey cell_key{
                    key.dimension_id,
                    static_cast<int32_t>(world_x),
                    static_cast<int32_t>(world_y),
                    static_cast<int32_t>(world_z),
                };
                emit_terrain_change(cell_key, previous, cell,
                                    FluidSimulationLayer::kDenseCellularAutomaton);
                wake_fluid_neighbors(cell_key.dimension_id, cell_key.block_x,
                                     cell_key.block_y, cell_key.block_z);
            }
        }
    }
    return changed;
}

bool GameFluidSystem::process_lattice_boltzmann_chunk(
    const snt::voxel::ChunkKey& key, uint64_t tick_index) {
    if (chunks_ == nullptr) return false;
    snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr || !can_use_lattice_boltzmann(key)) return false;

    const int32_t size_x = chunk->terrain.size_x;
    const int32_t size_y = chunk->terrain.size_y;
    const int32_t size_z = chunk->terrain.size_z;
    const size_t cell_count = chunk->terrain.cells.size();
    std::vector<FluidLbmCell> lbm_cells(cell_count);
    std::vector<FluidComputeCell> compute_cells(cell_count);
    for (size_t index = 0; index < cell_count; ++index) {
        const snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
        compute_cells[index] = {
            .is_solid = cell.is_solid(),
            .fluid_type = cell.fluid_type,
            .mass = cell.fluid_mass,
            .temperature = cell.fluid_temperature,
            .is_gas = cell.fluid_is_gas,
        };
        lbm_cells[index] = {
            .is_solid = cell.is_solid(),
            .mass = is_valid_fluid_state(cell) && !cell.fluid_is_gas
                        ? cell.fluid_mass
                        : static_cast<int16_t>(0),
        };
    }

    const auto to_world_coordinate = [](int64_t value) {
        return static_cast<int32_t>(std::clamp<int64_t>(
            value, std::numeric_limits<int32_t>::min(),
            std::numeric_limits<int32_t>::max()));
    };
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const GravityStep gravity = gravity_step(
        key.dimension_id,
        to_world_coordinate(static_cast<int64_t>(key.chunk_x) * kChunkSize + size_x / 2),
        to_world_coordinate(static_cast<int64_t>(key.chunk_y) * kChunkSize + size_y / 2),
        to_world_coordinate(static_cast<int64_t>(key.chunk_z) * kChunkSize + size_z / 2));
    const FluidLbmVelocity gravity_velocity{
        .x = static_cast<int16_t>(gravity.dx * config_.lattice_boltzmann_gravity_impulse),
        .y = static_cast<int16_t>(gravity.dy * config_.lattice_boltzmann_gravity_impulse),
        .z = static_cast<int16_t>(gravity.dz * config_.lattice_boltzmann_gravity_impulse),
    };
    const FluidLbmStepInput lbm_input{
        .size_x = size_x,
        .size_y = size_y,
        .size_z = size_z,
        .cells = std::span<const FluidLbmCell>(lbm_cells.data(), lbm_cells.size()),
        .gravity = gravity_velocity,
        .config = {},
    };

    std::vector<FluidLbmVelocity> velocity_field;
    bool accelerated = false;
    if (config_.allow_deterministic_compute_backend && compute_backend_ != nullptr &&
        compute_backend_->supports(FluidComputeKernel::kLatticeBoltzmann) &&
        compute_backend_->produces_deterministic_results()) {
        FluidComputeChunkResult result;
        const FluidComputeChunkRequest request{
            .chunk = key,
            .tick_index = tick_index,
            .kernel = FluidComputeKernel::kLatticeBoltzmann,
            .size_x = size_x,
            .size_y = size_y,
            .size_z = size_z,
            .cells = std::span<const FluidComputeCell>(compute_cells.data(),
                                                        compute_cells.size()),
            .gravity = gravity_velocity,
            .lattice_boltzmann_config = lbm_input.config,
        };
        if (compute_backend_->try_dispatch(request, result) &&
            result.velocity_field.size() == cell_count) {
            velocity_field = std::move(result.velocity_field);
            accelerated = true;
            ++last_telemetry_.accelerator_dispatches;
        }
    }
    if (!accelerated) {
        FluidLbmChunkState& state = lattice_boltzmann_states_[key];
        if (!lattice_boltzmann_solver_.step(lbm_input, state, velocity_field)) {
            return false;
        }
    }
    if (velocity_field.size() != cell_count) return false;
    last_telemetry_.lattice_boltzmann_cells_processed += static_cast<uint32_t>(
        std::min<size_t>(cell_count, std::numeric_limits<uint32_t>::max() -
                                    last_telemetry_.lattice_boltzmann_cells_processed));

    std::vector<DenseCellState> snapshot(cell_count);
    std::vector<int32_t> outgoing(cell_count, 0);
    std::vector<int32_t> incoming(cell_count, 0);
    std::vector<int64_t> incoming_temperature(cell_count, 0);
    std::vector<snt::voxel::CellFluidId> incoming_type(
        cell_count, snt::voxel::kInvalidCellFluidId);
    std::vector<bool> incoming_is_gas(cell_count, false);
    for (size_t index = 0; index < cell_count; ++index) {
        const snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
        snapshot[index] = {
            .fluid_type = cell.fluid_type,
            .mass = cell.fluid_mass,
            .temperature = cell.fluid_temperature,
            .is_gas = cell.fluid_is_gas,
        };
    }

    const auto local_index = [chunk](int32_t x, int32_t y, int32_t z) {
        return chunk->terrain.index_of(x, y, z);
    };
    const auto inside = [size_x, size_y, size_z](int32_t x, int32_t y, int32_t z) {
        return x >= 0 && x < size_x && y >= 0 && y < size_y && z >= 0 && z < size_z;
    };
    const auto world_coordinate = [](int32_t chunk_coordinate, int32_t local) {
        return static_cast<int32_t>(static_cast<int64_t>(chunk_coordinate) *
                                    snt::voxel::VoxelChunk::kChunkSize + local);
    };

    for (int32_t local_y = 1; local_y < size_y - 1; ++local_y) {
        for (int32_t local_z = 1; local_z < size_z - 1; ++local_z) {
            for (int32_t local_x = 1; local_x < size_x - 1; ++local_x) {
                const size_t source_index = local_index(local_x, local_y, local_z);
                const snt::voxel::TerrainCell& source_cell =
                    chunk->terrain.cells[source_index];
                const DenseCellState& source = snapshot[source_index];
                if (source_cell.is_solid() || source.mass <= 0 || source.is_gas ||
                    source.fluid_type == snt::voxel::kInvalidCellFluidId) {
                    continue;
                }

                const int32_t world_x = world_coordinate(key.chunk_x, local_x);
                const int32_t world_y = world_coordinate(key.chunk_y, local_y);
                const int32_t world_z = world_coordinate(key.chunk_z, local_z);
                const GravityStep local_gravity = gravity_step(key.dimension_id, world_x,
                                                               world_y, world_z);
                const auto plan_transfer = [&](int32_t target_x, int32_t target_y,
                                                int32_t target_z, int32_t requested) {
                    if (!inside(target_x, target_y, target_z) || requested <= 0) return false;
                    const size_t target_index = local_index(target_x, target_y, target_z);
                    const snt::voxel::TerrainCell& target_cell =
                        chunk->terrain.cells[target_index];
                    const DenseCellState& target = snapshot[target_index];
                    if (target_cell.is_solid() ||
                        (target.mass > 0 &&
                         (target.fluid_type != source.fluid_type || target.is_gas))) {
                        return false;
                    }
                    if (target.mass == 0 && incoming[target_index] > 0 &&
                        (incoming_type[target_index] != source.fluid_type ||
                         incoming_is_gas[target_index])) {
                        return false;
                    }
                    const int32_t available = source.mass - outgoing[source_index];
                    const int32_t target_mass = target.mass + incoming[target_index];
                    if (available <= 0 || target_mass >= snt::voxel::kCellFluidCapacity) {
                        return false;
                    }
                    const int32_t capacity = snt::voxel::kCellFluidCapacity - target_mass;
                    const int32_t moved = std::min({available, capacity, requested});
                    if (moved <= 0) return false;
                    outgoing[source_index] += moved;
                    incoming[target_index] += moved;
                    incoming_temperature[target_index] +=
                        static_cast<int64_t>(source.temperature) * moved;
                    if (target.mass == 0 && incoming[target_index] == moved) {
                        incoming_type[target_index] = source.fluid_type;
                        incoming_is_gas[target_index] = false;
                    }
                    return true;
                };

                if (plan_transfer(local_x + local_gravity.dx,
                                  local_y + local_gravity.dy,
                                  local_z + local_gravity.dz,
                                  source.mass)) {
                    continue;
                }

                const FluidLbmVelocity& velocity = velocity_field[source_index];
                int32_t best_x = 0;
                int32_t best_y = 0;
                int32_t best_z = 0;
                int32_t best_flow = std::numeric_limits<int32_t>::min();
                int32_t best_deficit = 0;
                int64_t best_score = std::numeric_limits<int64_t>::min();
                bool found = false;
                for (const auto& offset : kNeighborOffsets) {
                    const int32_t gravity_dot = offset[0] * local_gravity.dx +
                                                offset[1] * local_gravity.dy +
                                                offset[2] * local_gravity.dz;
                    if (gravity_dot != 0) continue;
                    const int32_t candidate_x = local_x + offset[0];
                    const int32_t candidate_y = local_y + offset[1];
                    const int32_t candidate_z = local_z + offset[2];
                    if (!inside(candidate_x, candidate_y, candidate_z)) continue;
                    const size_t target_index = local_index(candidate_x, candidate_y,
                                                            candidate_z);
                    const snt::voxel::TerrainCell& target_cell =
                        chunk->terrain.cells[target_index];
                    const DenseCellState& target = snapshot[target_index];
                    if (target_cell.is_solid() ||
                        (target.mass > 0 &&
                         (target.fluid_type != source.fluid_type || target.is_gas))) {
                        continue;
                    }
                    const int32_t available = source.mass - outgoing[source_index];
                    const int32_t target_mass = target.mass + incoming[target_index];
                    if (available <= 0 || target_mass >= snt::voxel::kCellFluidCapacity) {
                        continue;
                    }
                    const int32_t flow = offset[0] * velocity.x +
                                         offset[1] * velocity.y +
                                         offset[2] * velocity.z;
                    const int32_t deficit = available - target_mass;
                    if (deficit <= 0 && flow <= 0) continue;
                    const int64_t score = static_cast<int64_t>(flow) * 4 + deficit;
                    if (!found || score > best_score) {
                        found = true;
                        best_score = score;
                        best_x = candidate_x;
                        best_y = candidate_y;
                        best_z = candidate_z;
                        best_flow = flow;
                        best_deficit = deficit;
                    }
                }
                if (!found) continue;
                const int32_t available = source.mass - outgoing[source_index];
                int32_t requested = std::max<int32_t>(0, best_deficit / 2);
                if (best_flow > 0) {
                    requested = std::max<int32_t>(requested, std::max<int32_t>(
                        1, static_cast<int32_t>(static_cast<int64_t>(available) * best_flow /
                                                (2 * kFluidLbmVelocityScale))));
                }
                static_cast<void>(plan_transfer(best_x, best_y, best_z, requested));
            }
        }
    }

    bool changed = false;
    for (int32_t local_y = 0; local_y < size_y; ++local_y) {
        for (int32_t local_z = 0; local_z < size_z; ++local_z) {
            for (int32_t local_x = 0; local_x < size_x; ++local_x) {
                const size_t index = local_index(local_x, local_y, local_z);
                if (outgoing[index] == 0 && incoming[index] == 0) continue;
                snt::voxel::TerrainCell& cell = chunk->terrain.cells[index];
                if (cell.is_solid()) continue;
                const snt::voxel::TerrainCell previous = cell;
                const DenseCellState& initial = snapshot[index];
                const int32_t final_mass = initial.mass - outgoing[index] + incoming[index];
                if (final_mass <= 0) {
                    cell.clear_fluid();
                } else {
                    const int32_t retained_mass = std::max<int32_t>(
                        0, initial.mass - outgoing[index]);
                    const int64_t temperature_sum =
                        static_cast<int64_t>(initial.temperature) * retained_mass +
                        incoming_temperature[index];
                    cell.fluid_mass = static_cast<int16_t>(final_mass);
                    cell.fluid_type = initial.mass > 0
                        ? initial.fluid_type
                        : incoming_type[index];
                    cell.fluid_is_gas = false;
                    cell.fluid_temperature = static_cast<int16_t>(
                        temperature_sum / std::max<int32_t>(1, final_mass));
                }
                if (same_terrain_cell(previous, cell)) continue;
                changed = true;
                const int64_t world_x = static_cast<int64_t>(key.chunk_x) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_x;
                const int64_t world_y = static_cast<int64_t>(key.chunk_y) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_y;
                const int64_t world_z = static_cast<int64_t>(key.chunk_z) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_z;
                if (world_x < std::numeric_limits<int32_t>::min() ||
                    world_x > std::numeric_limits<int32_t>::max() ||
                    world_y < std::numeric_limits<int32_t>::min() ||
                    world_y > std::numeric_limits<int32_t>::max() ||
                    world_z < std::numeric_limits<int32_t>::min() ||
                    world_z > std::numeric_limits<int32_t>::max()) {
                    continue;
                }
                const FluidCellKey cell_key{
                    key.dimension_id,
                    static_cast<int32_t>(world_x),
                    static_cast<int32_t>(world_y),
                    static_cast<int32_t>(world_z),
                };
                emit_terrain_change(cell_key, previous, cell,
                                    FluidSimulationLayer::kLatticeBoltzmann);
                // Preserve a sparse handoff set if the chunk later reaches a
                // boundary or mixture that requires the CA fallback.
                active_cells_.insert(cell_key);
            }
        }
    }
    return changed;
}

bool GameFluidSystem::dense_chunk_has_cross_boundary_flow(
    const snt::voxel::ChunkKey& key) {
    if (chunks_ == nullptr) return false;
    snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) return false;

    const auto world_coordinate = [](int32_t chunk_coordinate, int32_t local) {
        return static_cast<int32_t>(static_cast<int64_t>(chunk_coordinate) *
                                    snt::voxel::VoxelChunk::kChunkSize + local);
    };
    const auto candidate_can_flow = [this, &key, &world_coordinate, chunk](
                                        const snt::voxel::TerrainCell& source,
                                        int32_t local_x, int32_t local_y,
                                        int32_t local_z, int32_t dx, int32_t dy,
                                        int32_t dz, bool require_lower_mass) {
        const int32_t next_x = local_x + dx;
        const int32_t next_y = local_y + dy;
        const int32_t next_z = local_z + dz;
        if (next_x >= 0 && next_x < chunk->terrain.size_x &&
            next_y >= 0 && next_y < chunk->terrain.size_y &&
            next_z >= 0 && next_z < chunk->terrain.size_z) {
            return false;
        }
        int32_t world_x = world_coordinate(key.chunk_x, local_x);
        int32_t world_y = world_coordinate(key.chunk_y, local_y);
        int32_t world_z = world_coordinate(key.chunk_z, local_z);
        if (!offset_coordinate(world_x, dx, world_x) ||
            !offset_coordinate(world_y, dy, world_y) ||
            !offset_coordinate(world_z, dz, world_z)) {
            return false;
        }
        const ResolvedCell target = resolve_cell(key.dimension_id, world_x, world_y, world_z);
        return target.cell != nullptr && can_flow_into(source, *target.cell,
                                                        require_lower_mass);
    };

    for (int32_t local_y = 0; local_y < chunk->terrain.size_y; ++local_y) {
        for (int32_t local_z = 0; local_z < chunk->terrain.size_z; ++local_z) {
            for (int32_t local_x = 0; local_x < chunk->terrain.size_x; ++local_x) {
                if (local_x != 0 && local_y != 0 && local_z != 0 &&
                    local_x != chunk->terrain.size_x - 1 &&
                    local_y != chunk->terrain.size_y - 1 &&
                    local_z != chunk->terrain.size_z - 1) {
                    continue;
                }
                const snt::voxel::TerrainCell& source = chunk->terrain.cell_at(
                    local_x, local_y, local_z);
                if (!is_valid_fluid_state(source)) continue;
                if (source.fluid_is_gas) {
                    for (const auto& offset : kNeighborOffsets) {
                        if (candidate_can_flow(source, local_x, local_y, local_z,
                                               offset[0], offset[1], offset[2], true)) {
                            return true;
                        }
                    }
                    continue;
                }
                const int32_t world_x = world_coordinate(key.chunk_x, local_x);
                const int32_t world_y = world_coordinate(key.chunk_y, local_y);
                const int32_t world_z = world_coordinate(key.chunk_z, local_z);
                const GravityStep gravity = gravity_step(key.dimension_id, world_x,
                                                         world_y, world_z);
                if (candidate_can_flow(source, local_x, local_y, local_z,
                                       gravity.dx, gravity.dy, gravity.dz, false)) {
                    return true;
                }
                for (const auto& offset : kNeighborOffsets) {
                    const int32_t dot = offset[0] * gravity.dx + offset[1] * gravity.dy +
                                        offset[2] * gravity.dz;
                    if (dot != 0) continue;
                    if (candidate_can_flow(source, local_x, local_y, local_z,
                                           offset[0], offset[1], offset[2], true)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void GameFluidSystem::settle_equilibrium_chunk(const snt::voxel::ChunkKey& key) {
    if (chunks_ == nullptr || chunk_has_active_cells(key) || chunk_is_dense(key)) return;
    snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (chunk == nullptr) {
        equilibrium_chunks_.erase(key);
        return;
    }

    FluidEquilibriumSummary summary;
    bool has_type = false;
    int64_t temperature_sum = 0;
    for (int32_t local_y = 0; local_y < chunk->terrain.size_y; ++local_y) {
        for (int32_t local_z = 0; local_z < chunk->terrain.size_z; ++local_z) {
            for (int32_t local_x = 0; local_x < chunk->terrain.size_x; ++local_x) {
                const snt::voxel::TerrainCell& cell = chunk->terrain.cell_at(
                    local_x, local_y, local_z);
                if (!is_valid_fluid_state(cell)) continue;
                if (!has_type) {
                    summary.fluid_type = cell.fluid_type;
                    has_type = true;
                } else if (summary.fluid_type != cell.fluid_type) {
                    summary.fluid_type = snt::voxel::kInvalidCellFluidId;
                }
                ++summary.fluid_cell_count;
                summary.total_fluid_mass += cell.fluid_mass;
                temperature_sum += static_cast<int64_t>(cell.fluid_temperature) * cell.fluid_mass;
                const int64_t world_y = static_cast<int64_t>(key.chunk_y) *
                                            snt::voxel::VoxelChunk::kChunkSize + local_y;
                if (world_y >= std::numeric_limits<int32_t>::min() &&
                    world_y <= std::numeric_limits<int32_t>::max()) {
                    summary.surface_block_y = std::max(summary.surface_block_y,
                                                       static_cast<int32_t>(world_y));
                }
            }
        }
    }
    if (summary.fluid_cell_count == 0 || summary.total_fluid_mass == 0) {
        equilibrium_chunks_.erase(key);
        return;
    }
    summary.average_temperature = static_cast<int16_t>(
        temperature_sum / summary.total_fluid_mass);
    equilibrium_chunks_.insert_or_assign(key, summary);
}

void GameFluidSystem::process_sparse_cells() {
    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());
    std::sort(cells.begin(), cells.end(), [](const FluidCellKey& left,
                                             const FluidCellKey& right) {
        if (left.dimension_id != right.dimension_id) {
            return left.dimension_id < right.dimension_id;
        }
        if (left.block_y != right.block_y) return left.block_y < right.block_y;
        if (left.block_x != right.block_x) return left.block_x < right.block_x;
        return left.block_z < right.block_z;
    });

    uint32_t processed = 0;
    for (const FluidCellKey& key : cells) {
        if (processed >= config_.max_sparse_cells_per_tick) break;
        const snt::voxel::ChunkKey chunk = chunk_key_for(
            key.dimension_id, key.block_x, key.block_y, key.block_z);
        if (chunk_is_dense(chunk)) continue;
        ++processed;
        ++last_telemetry_.sparse_cells_processed;
        if (process_sparse_cell(key)) continue;
        active_cells_.erase(key);
        equilibrium_candidates_.insert(chunk);
    }
}

bool GameFluidSystem::process_sparse_cell(const FluidCellKey& key) {
    ResolvedCell source = resolve_cell(key.dimension_id, key.block_x,
                                       key.block_y, key.block_z);
    if (source.cell == nullptr || !is_valid_fluid_state(*source.cell)) return false;

    const snt::voxel::TerrainCell source_snapshot = *source.cell;
    if (source_snapshot.fluid_is_gas) {
        FluidCellKey best;
        int32_t best_mass = std::numeric_limits<int32_t>::max();
        bool found = false;
        for (const auto& offset : kNeighborOffsets) {
            int32_t target_x = 0;
            int32_t target_y = 0;
            int32_t target_z = 0;
            if (!offset_coordinate(key.block_x, offset[0], target_x) ||
                !offset_coordinate(key.block_y, offset[1], target_y) ||
                !offset_coordinate(key.block_z, offset[2], target_z)) {
                continue;
            }
            const ResolvedCell target = resolve_cell(key.dimension_id, target_x,
                                                     target_y, target_z);
            if (target.cell == nullptr ||
                !can_flow_into(source_snapshot, *target.cell, true)) {
                continue;
            }
            if (target.cell->fluid_mass < best_mass) {
                best = {key.dimension_id, target_x, target_y, target_z};
                best_mass = target.cell->fluid_mass;
                found = true;
            }
        }
        return found && try_transfer(key, best,
                                     static_cast<int16_t>((source_snapshot.fluid_mass -
                                                           best_mass) / 2),
                                     FluidSimulationLayer::kSparse);
    }

    const GravityStep gravity = gravity_step(key.dimension_id, key.block_x,
                                             key.block_y, key.block_z);
    int32_t down_x = 0;
    int32_t down_y = 0;
    int32_t down_z = 0;
    if (offset_coordinate(key.block_x, gravity.dx, down_x) &&
        offset_coordinate(key.block_y, gravity.dy, down_y) &&
        offset_coordinate(key.block_z, gravity.dz, down_z)) {
        const FluidCellKey down{key.dimension_id, down_x, down_y, down_z};
        const ResolvedCell target = resolve_cell(key.dimension_id, down_x, down_y, down_z);
        if (target.cell != nullptr && can_flow_into(source_snapshot, *target.cell, false) &&
            try_transfer(key, down, source_snapshot.fluid_mass,
                         FluidSimulationLayer::kSparse)) {
            return true;
        }
    }

    FluidCellKey best;
    int32_t best_mass = std::numeric_limits<int32_t>::max();
    bool found = false;
    for (const auto& offset : kNeighborOffsets) {
        const int32_t dot = offset[0] * gravity.dx + offset[1] * gravity.dy +
                            offset[2] * gravity.dz;
        if (dot != 0) continue;
        int32_t target_x = 0;
        int32_t target_y = 0;
        int32_t target_z = 0;
        if (!offset_coordinate(key.block_x, offset[0], target_x) ||
            !offset_coordinate(key.block_y, offset[1], target_y) ||
            !offset_coordinate(key.block_z, offset[2], target_z)) {
            continue;
        }
        const ResolvedCell target = resolve_cell(key.dimension_id, target_x,
                                                 target_y, target_z);
        if (target.cell == nullptr ||
            !can_flow_into(source_snapshot, *target.cell, true)) {
            continue;
        }
        if (target.cell->fluid_mass < best_mass) {
            best = {key.dimension_id, target_x, target_y, target_z};
            best_mass = target.cell->fluid_mass;
            found = true;
        }
    }
    return found && try_transfer(key, best,
                                 static_cast<int16_t>((source_snapshot.fluid_mass -
                                                       best_mass) / 2),
                                 FluidSimulationLayer::kSparse);
}

bool GameFluidSystem::try_transfer(const FluidCellKey& source_key,
                                   const FluidCellKey& destination_key,
                                   int16_t requested_amount,
                                   FluidSimulationLayer layer) {
    if (requested_amount <= 0) return false;
    ResolvedCell source = resolve_cell(source_key.dimension_id, source_key.block_x,
                                       source_key.block_y, source_key.block_z);
    ResolvedCell destination = resolve_cell(destination_key.dimension_id,
                                            destination_key.block_x,
                                            destination_key.block_y,
                                            destination_key.block_z);
    if (source.cell == nullptr || destination.cell == nullptr ||
        !can_receive(*source.cell, *destination.cell)) {
        return false;
    }

    const snt::voxel::TerrainCell source_before = *source.cell;
    const snt::voxel::TerrainCell destination_before = *destination.cell;
    const int16_t amount = std::min<int16_t>(requested_amount,
                                             source_before.fluid_mass);
    const int16_t inserted = destination.cell->insert_fluid(
        source_before.fluid_type, amount, source_before.fluid_is_gas);
    if (inserted <= 0) return false;
    source.cell->extract_fluid(inserted);
    if (destination_before.fluid_mass == 0) {
        destination.cell->fluid_temperature = source_before.fluid_temperature;
    } else {
        const int32_t weighted_temperature =
            static_cast<int32_t>(destination_before.fluid_temperature) *
                destination_before.fluid_mass +
            static_cast<int32_t>(source_before.fluid_temperature) * inserted;
        destination.cell->fluid_temperature = static_cast<int16_t>(
            weighted_temperature / destination.cell->fluid_mass);
    }

    emit_terrain_change(source_key, source_before, *source.cell, layer);
    emit_terrain_change(destination_key, destination_before, *destination.cell, layer);
    wake_cell(source_key.dimension_id, source_key.block_x, source_key.block_y,
              source_key.block_z);
    wake_cell(destination_key.dimension_id, destination_key.block_x,
              destination_key.block_y, destination_key.block_z);
    wake_fluid_neighbors(source_key.dimension_id, source_key.block_x,
                         source_key.block_y, source_key.block_z);
    wake_fluid_neighbors(destination_key.dimension_id, destination_key.block_x,
                         destination_key.block_y, destination_key.block_z);
    return true;
}

bool GameFluidSystem::can_receive(const snt::voxel::TerrainCell& source,
                                  const snt::voxel::TerrainCell& destination) const noexcept {
    return is_valid_fluid_state(source) && !destination.is_solid() &&
           destination.fluid_remaining_space() > 0 &&
           (!destination.has_fluid() ||
            (destination.fluid_type == source.fluid_type &&
             destination.fluid_is_gas == source.fluid_is_gas));
}

bool GameFluidSystem::can_flow_into(const snt::voxel::TerrainCell& source,
                                    const snt::voxel::TerrainCell& destination,
                                    bool require_lower_mass) const noexcept {
    return can_receive(source, destination) &&
           (!require_lower_mass || source.fluid_mass > destination.fluid_mass);
}

void GameFluidSystem::update_equilibrium_candidates() {
    std::vector<snt::voxel::ChunkKey> candidates(equilibrium_candidates_.begin(),
                                                  equilibrium_candidates_.end());
    equilibrium_candidates_.clear();
    for (const snt::voxel::ChunkKey& chunk : candidates) {
        if (chunk_is_dense(chunk) || chunk_has_active_cells(chunk)) {
            equilibrium_chunks_.erase(chunk);
            continue;
        }
        settle_equilibrium_chunk(chunk);
    }
}

void GameFluidSystem::flush_presentation_updates(uint64_t tick_index) {
    if (presentation_dirty_chunks_.empty()) return;
    if (presentation_sink_ == nullptr) {
        presentation_dirty_chunks_.clear();
        return;
    }
    for (const auto& [key, layer] : presentation_dirty_chunks_) {
        if (chunks_ == nullptr) continue;
        const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr) continue;
        FluidPresentationChunk update;
        update.chunk = key;
        update.layer = layer;
        update.tick_index = tick_index;
        for (const snt::voxel::TerrainCell& cell : chunk->terrain.cells) {
            if (!is_valid_fluid_state(cell)) continue;
            ++update.fluid_cell_count;
            update.total_fluid_mass += cell.fluid_mass;
        }
        presentation_sink_->on_fluid_presentation_chunk_dirty(update);
    }
    presentation_dirty_chunks_.clear();
}

void GameFluidSystem::emit_low_frequency_telemetry(uint64_t tick_index) {
    if (config_.telemetry_interval_ticks == 0 ||
        (last_telemetry_tick_ != 0 &&
         tick_index - last_telemetry_tick_ < config_.telemetry_interval_ticks)) {
        return;
    }
    last_telemetry_tick_ = tick_index;
    if (telemetry_sink_ != nullptr) {
        telemetry_sink_->on_fluid_simulation_telemetry(last_telemetry_);
    }
    if (last_telemetry_.active_cells_after == 0 &&
        last_telemetry_.dense_chunks_processed == 0 &&
        last_telemetry_.lattice_boltzmann_chunks_processed == 0 &&
        last_telemetry_.terrain_mutations == 0) {
        return;
    }
    SNT_LOG_INFO(
        "Fluid tick=%llu active=%u->%u sparse=%u dense_chunks=%u dense_cells=%u "
        "lbm_chunks=%u lbm_cells=%u accelerator=%u equilibrium=%u mutations=%u",
        static_cast<unsigned long long>(tick_index),
        last_telemetry_.active_cells_before,
        last_telemetry_.active_cells_after,
        last_telemetry_.sparse_cells_processed,
        last_telemetry_.dense_chunks_processed,
        last_telemetry_.dense_cells_processed,
        last_telemetry_.lattice_boltzmann_chunks_processed,
        last_telemetry_.lattice_boltzmann_cells_processed,
        last_telemetry_.accelerator_dispatches,
        last_telemetry_.equilibrium_chunks,
        last_telemetry_.terrain_mutations);
}

}  // namespace snt::game
