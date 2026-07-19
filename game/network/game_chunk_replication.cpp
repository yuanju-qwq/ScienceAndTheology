// Game-owned terrain and machine replication payload implementation.

#include "game/network/game_chunk_replication.h"

#include "core/error.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr uint8_t kGameTerrainChunkPayloadVersion = 1;
constexpr size_t kMaxReplicatedMachineSlots = 64;
constexpr size_t kMaxReplicatedMachineIdBytes = 512;
constexpr size_t kMaxReplicatedItemIdBytes = 512;
constexpr int32_t kMaxReplicatedMachineValue = 1'000'000'000;
constexpr uint8_t kMachineRunStateCount = 6;

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(uint32_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool is_valid_chunk_key(const snt::voxel::ChunkKey& key) noexcept {
    return !key.dimension_id.empty() &&
           key.dimension_id.size() <= kMaxGameDimensionIdBytes &&
           !has_embedded_nul(key.dimension_id);
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if ((require_non_empty && value.empty()) || has_embedded_nul(value) ||
        value.size() > maximum || value.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error(std::string("Game machine ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Game machine ") + field_name + " is truncated");
    }
    const size_t length = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && length == 0) || length > maximum ||
        bytes.size() - offset < length) {
        return protocol_error(std::string("Game machine ") + field_name + " is invalid");
    }
    std::string value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += length;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("Game machine ") + field_name + " contains a NUL byte");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> append_chunk_key(
    std::vector<std::byte>& bytes, const snt::voxel::ChunkKey& key) {
    if (!is_valid_chunk_key(key)) return protocol_error("Game machine anchor chunk is invalid");
    if (auto result = append_short_string(bytes, key.dimension_id,
                                          kMaxGameDimensionIdBytes,
                                          "anchor dimension id", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, key.chunk_x);
    append_i32(bytes, key.chunk_y);
    append_i32(bytes, key.chunk_z);
    return {};
}

[[nodiscard]] snt::core::Expected<snt::voxel::ChunkKey> read_chunk_key(
    std::span<const std::byte> bytes, size_t& offset) {
    auto dimension = read_short_string(bytes, offset, kMaxGameDimensionIdBytes,
                                       "anchor dimension id", true);
    if (!dimension) return dimension.error();
    constexpr size_t kCoordinatesBytes = sizeof(uint32_t) * 3;
    if (bytes.size() - offset < kCoordinatesBytes) {
        return protocol_error("Game machine anchor chunk coordinates are truncated");
    }
    snt::voxel::ChunkKey key{
        std::move(*dimension),
        read_i32(bytes, offset),
        read_i32(bytes, offset + sizeof(uint32_t)),
        read_i32(bytes, offset + sizeof(uint32_t) * 2),
    };
    offset += kCoordinatesBytes;
    if (!is_valid_chunk_key(key)) return protocol_error("Game machine anchor chunk is invalid");
    return key;
}

[[nodiscard]] bool is_valid_machine_operation(
    GameMachineReplicationOperation operation) noexcept {
    return operation == GameMachineReplicationOperation::kUpsert ||
           operation == GameMachineReplicationOperation::kRemove;
}

[[nodiscard]] snt::core::Expected<void> validate_machine_stack(
    const GameReplicatedMachineItemStack& stack) {
    if (stack.item_id.empty()) {
        if (stack.count != 0) {
            return protocol_error("Game machine empty stack has a nonzero count");
        }
        return {};
    }
    if (stack.item_id.size() > kMaxReplicatedItemIdBytes ||
        has_embedded_nul(stack.item_id) || stack.count <= 0 ||
        stack.count > kMaxReplicatedMachineValue) {
        return protocol_error("Game machine item stack is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_machine_state(
    const GameReplicatedMachineState& machine) {
    if (!is_valid_chunk_key(machine.anchor_chunk) || machine.machine_id.empty() ||
        machine.machine_id.size() > kMaxReplicatedMachineIdBytes ||
        has_embedded_nul(machine.machine_id) ||
        machine.max_input_slots == 0 ||
        machine.max_input_slots > kMaxReplicatedMachineSlots ||
        machine.max_output_slots == 0 ||
        machine.max_output_slots > kMaxReplicatedMachineSlots ||
        machine.input_slots.size() > kMaxReplicatedMachineSlots ||
        machine.output_slots.size() > kMaxReplicatedMachineSlots ||
        machine.input_slots.size() > machine.max_input_slots ||
        machine.output_slots.size() > machine.max_output_slots ||
        machine.stored_energy < 0 || machine.energy_capacity < 0 ||
        machine.stored_energy > machine.energy_capacity || machine.progress_ticks < 0 ||
        machine.active_recipe_duration_ticks < 0 ||
        machine.progress_ticks > kMaxReplicatedMachineValue ||
        machine.active_recipe_duration_ticks > kMaxReplicatedMachineValue ||
        machine.run_state >= kMachineRunStateCount) {
        return protocol_error("Game machine presentation state is invalid");
    }
    for (const GameReplicatedMachineItemStack& stack : machine.input_slots) {
        if (auto result = validate_machine_stack(stack); !result) return result.error();
    }
    for (const GameReplicatedMachineItemStack& stack : machine.output_slots) {
        if (auto result = validate_machine_stack(stack); !result) return result.error();
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_machine_stack(
    std::vector<std::byte>& bytes, const GameReplicatedMachineItemStack& stack) {
    if (auto result = validate_machine_stack(stack); !result) return result.error();
    if (auto result = append_short_string(bytes, stack.item_id,
                                          kMaxReplicatedItemIdBytes,
                                          "item id", false);
        !result) {
        return result.error();
    }
    append_i32(bytes, stack.count);
    return {};
}

[[nodiscard]] snt::core::Expected<GameReplicatedMachineItemStack> read_machine_stack(
    std::span<const std::byte> bytes, size_t& offset) {
    auto item_id = read_short_string(bytes, offset, kMaxReplicatedItemIdBytes,
                                     "item id", false);
    if (!item_id) return item_id.error();
    if (bytes.size() - offset < sizeof(uint32_t)) {
        return protocol_error("Game machine item stack is truncated");
    }
    GameReplicatedMachineItemStack stack{
        .item_id = std::move(*item_id),
        .count = read_i32(bytes, offset),
    };
    offset += sizeof(uint32_t);
    if (auto result = validate_machine_stack(stack); !result) return result.error();
    return stack;
}

[[nodiscard]] snt::core::Expected<void> append_machine_stacks(
    std::vector<std::byte>& bytes, const std::vector<GameReplicatedMachineItemStack>& stacks,
    const char* field_name) {
    if (stacks.size() > kMaxReplicatedMachineSlots ||
        stacks.size() > std::numeric_limits<uint8_t>::max()) {
        return protocol_error(std::string("Game machine ") + field_name + " exceeds the slot limit");
    }
    bytes.push_back(static_cast<std::byte>(stacks.size()));
    for (const GameReplicatedMachineItemStack& stack : stacks) {
        if (auto result = append_machine_stack(bytes, stack); !result) return result.error();
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::vector<GameReplicatedMachineItemStack>>
read_machine_stacks(std::span<const std::byte> bytes, size_t& offset, const char* field_name) {
    if (offset >= bytes.size()) {
        return protocol_error(std::string("Game machine ") + field_name + " is truncated");
    }
    const size_t count = std::to_integer<uint8_t>(bytes[offset++]);
    if (count > kMaxReplicatedMachineSlots) {
        return protocol_error(std::string("Game machine ") + field_name + " exceeds the slot limit");
    }
    std::vector<GameReplicatedMachineItemStack> stacks;
    stacks.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        auto stack = read_machine_stack(bytes, offset);
        if (!stack) return stack.error();
        stacks.push_back(std::move(*stack));
    }
    return stacks;
}

[[nodiscard]] snt::core::Expected<void> validate_terrain_chunk(
    const GameReplicatedTerrainChunk& terrain) {
    if (terrain.size_x <= 0 || terrain.size_y <= 0 || terrain.size_z <= 0 ||
        terrain.size_x > snt::voxel::VoxelChunk::kChunkSize ||
        terrain.size_y > snt::voxel::VoxelChunk::kChunkSize ||
        terrain.size_z > snt::voxel::VoxelChunk::kChunkSize) {
        return protocol_error("Game terrain chunk dimensions are invalid");
    }
    const size_t expected = static_cast<size_t>(terrain.size_x) *
                            static_cast<size_t>(terrain.size_y) *
                            static_cast<size_t>(terrain.size_z);
    if (expected == 0 || expected > kMaxGameBlockDeltasPerChunk ||
        terrain.cells.size() != expected) {
        return protocol_error("Game terrain chunk cell count is invalid");
    }
    return {};
}

[[nodiscard]] bool same_machine_stack(const GameReplicatedMachineItemStack& left,
                                       const GameReplicatedMachineItemStack& right) noexcept {
    return left.item_id == right.item_id && left.count == right.count;
}

}  // namespace

bool GameChunkKeyLess::operator()(const snt::voxel::ChunkKey& left,
                                  const snt::voxel::ChunkKey& right) const noexcept {
    if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
    if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
    if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
    return left.chunk_z < right.chunk_z;
}

bool same_game_replicated_terrain_cell(const GameReplicatedTerrainCell& left,
                                       const GameReplicatedTerrainCell& right) noexcept {
    return left.material == right.material && left.flags == right.flags;
}

snt::core::Expected<GameReplicatedTerrainChunk>
make_game_replicated_terrain_chunk(const snt::voxel::VoxelChunk& chunk) {
    GameReplicatedTerrainChunk terrain{
        .size_x = chunk.terrain.size_x,
        .size_y = chunk.terrain.size_y,
        .size_z = chunk.terrain.size_z,
    };
    terrain.cells.reserve(chunk.terrain.cells.size());
    for (const snt::voxel::TerrainCell& cell : chunk.terrain.cells) {
        terrain.cells.push_back({.material = cell.material, .flags = cell.flags});
    }
    if (auto result = validate_terrain_chunk(terrain); !result) return result.error();
    return terrain;
}

snt::core::Expected<snt::voxel::VoxelChunk>
make_voxel_chunk_from_game_replicated_terrain(const snt::voxel::ChunkKey& key,
                                              const GameReplicatedTerrainChunk& terrain) {
    if (!is_valid_chunk_key(key)) return protocol_error("Game terrain chunk key is invalid");
    if (auto result = validate_terrain_chunk(terrain); !result) return result.error();
    snt::voxel::VoxelChunk chunk;
    chunk.chunk_x = key.chunk_x;
    chunk.chunk_y = key.chunk_y;
    chunk.chunk_z = key.chunk_z;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(terrain.size_x, terrain.size_y, terrain.size_z);
    for (size_t index = 0; index < terrain.cells.size(); ++index) {
        chunk.terrain.cells[index].material = terrain.cells[index].material;
        chunk.terrain.cells[index].flags = terrain.cells[index].flags;
    }
    return chunk;
}

snt::core::Expected<std::vector<std::byte>>
encode_game_terrain_chunk_snapshot(const snt::voxel::VoxelChunk& chunk) {
    auto terrain = make_game_replicated_terrain_chunk(chunk);
    if (!terrain) return terrain.error();

    std::vector<std::byte> bytes;
    bytes.reserve(6 + terrain->cells.size() * 2);
    bytes.push_back(static_cast<std::byte>(kGameTerrainChunkPayloadVersion));
    bytes.push_back(static_cast<std::byte>(terrain->size_x));
    bytes.push_back(static_cast<std::byte>(terrain->size_y));
    bytes.push_back(static_cast<std::byte>(terrain->size_z));

    struct Run {
        GameReplicatedTerrainCell cell;
        uint16_t length = 0;
    };
    std::vector<Run> runs;
    runs.reserve(terrain->cells.size());
    for (const GameReplicatedTerrainCell& cell : terrain->cells) {
        if (!runs.empty() && same_game_replicated_terrain_cell(runs.back().cell, cell) &&
            runs.back().length < std::numeric_limits<uint16_t>::max()) {
            ++runs.back().length;
        } else {
            runs.push_back({.cell = cell, .length = 1});
        }
    }
    if (runs.empty() || runs.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error("Game terrain chunk run count is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(runs.size()));
    for (const Run& run : runs) {
        bytes.push_back(static_cast<std::byte>(run.cell.material));
        append_u32(bytes, run.cell.flags);
        append_u16(bytes, run.length);
    }
    if (bytes.size() > kMaxGameChunkSnapshotPayloadBytes) {
        return protocol_error("Game terrain chunk snapshot exceeds the payload limit");
    }
    return bytes;
}

snt::core::Expected<GameReplicatedTerrainChunk>
decode_game_terrain_chunk_snapshot(std::span<const std::byte> payload) {
    constexpr size_t kHeaderBytes = sizeof(uint8_t) * 4 + sizeof(uint16_t);
    constexpr size_t kRunBytes = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t);
    if (payload.size() < kHeaderBytes ||
        std::to_integer<uint8_t>(payload[0]) != kGameTerrainChunkPayloadVersion) {
        return protocol_error("Game terrain chunk snapshot header is invalid");
    }

    GameReplicatedTerrainChunk terrain{
        .size_x = std::to_integer<uint8_t>(payload[1]),
        .size_y = std::to_integer<uint8_t>(payload[2]),
        .size_z = std::to_integer<uint8_t>(payload[3]),
    };
    if (terrain.size_x <= 0 || terrain.size_y <= 0 || terrain.size_z <= 0 ||
        terrain.size_x > snt::voxel::VoxelChunk::kChunkSize ||
        terrain.size_y > snt::voxel::VoxelChunk::kChunkSize ||
        terrain.size_z > snt::voxel::VoxelChunk::kChunkSize) {
        return protocol_error("Game terrain chunk snapshot dimensions are invalid");
    }
    const size_t expected_cells = static_cast<size_t>(terrain.size_x) *
                                  static_cast<size_t>(terrain.size_y) *
                                  static_cast<size_t>(terrain.size_z);
    const size_t run_count = read_u16(payload, sizeof(uint8_t) * 4);
    if (run_count == 0 || run_count > expected_cells ||
        payload.size() - kHeaderBytes != run_count * kRunBytes) {
        return protocol_error("Game terrain chunk snapshot run layout is invalid");
    }
    terrain.cells.reserve(expected_cells);
    size_t offset = kHeaderBytes;
    for (size_t index = 0; index < run_count; ++index) {
        const GameReplicatedTerrainCell cell{
            .material = std::to_integer<snt::voxel::TerrainMaterialId>(payload[offset]),
            .flags = read_u32(payload, offset + sizeof(uint8_t)),
        };
        const size_t length = read_u16(payload, offset + sizeof(uint8_t) + sizeof(uint32_t));
        offset += kRunBytes;
        if (length == 0 || length > expected_cells - terrain.cells.size()) {
            return protocol_error("Game terrain chunk snapshot run length is invalid");
        }
        terrain.cells.insert(terrain.cells.end(), length, cell);
    }
    if (terrain.cells.size() != expected_cells) {
        return protocol_error("Game terrain chunk snapshot cell count is invalid");
    }
    if (auto result = validate_terrain_chunk(terrain); !result) return result.error();
    return terrain;
}

bool is_game_machine_replication_entity_payload(std::span<const std::byte> payload) noexcept {
    return !payload.empty() &&
           std::to_integer<uint8_t>(payload.front()) == kGameMachineReplicationEntityKind;
}

snt::core::Expected<std::vector<std::byte>>
encode_game_machine_replication_entity(const GameMachineReplicationEntity& entity) {
    if (!is_valid_machine_operation(entity.operation)) {
        return protocol_error("Game machine replication operation is invalid");
    }
    if (entity.operation == GameMachineReplicationOperation::kRemove) {
        if (entity.machine.has_value()) {
            return protocol_error("Game machine remove carries presentation state");
        }
        return std::vector<std::byte>{
            static_cast<std::byte>(kGameMachineReplicationEntityKind),
            static_cast<std::byte>(kGameMachineReplicationEntityVersion),
            static_cast<std::byte>(entity.operation),
            std::byte{0},
        };
    }
    if (!entity.machine.has_value()) {
        return protocol_error("Game machine upsert has no presentation state");
    }
    if (auto result = validate_machine_state(*entity.machine); !result) return result.error();

    const GameReplicatedMachineState& machine = *entity.machine;
    std::vector<std::byte> bytes;
    bytes.reserve(128);
    bytes.push_back(static_cast<std::byte>(kGameMachineReplicationEntityKind));
    bytes.push_back(static_cast<std::byte>(kGameMachineReplicationEntityVersion));
    bytes.push_back(static_cast<std::byte>(entity.operation));
    bytes.push_back(std::byte{0});
    if (auto result = append_chunk_key(bytes, machine.anchor_chunk); !result) return result.error();
    append_i32(bytes, machine.root_x);
    append_i32(bytes, machine.root_y);
    append_i32(bytes, machine.root_z);
    if (auto result = append_short_string(bytes, machine.machine_id,
                                          kMaxReplicatedMachineIdBytes,
                                          "id", true);
        !result) {
        return result.error();
    }
    bytes.push_back(static_cast<std::byte>(machine.run_state));
    bytes.push_back(static_cast<std::byte>(machine.max_input_slots));
    bytes.push_back(static_cast<std::byte>(machine.max_output_slots));
    bytes.push_back(std::byte{0});
    append_i32(bytes, machine.stored_energy);
    append_i32(bytes, machine.energy_capacity);
    append_i32(bytes, machine.progress_ticks);
    append_i32(bytes, machine.active_recipe_duration_ticks);
    if (auto result = append_machine_stacks(bytes, machine.input_slots, "input slots"); !result) {
        return result.error();
    }
    if (auto result = append_machine_stacks(bytes, machine.output_slots, "output slots"); !result) {
        return result.error();
    }
    if (bytes.size() > kMaxGameEntitySnapshotPayloadBytes) {
        return protocol_error("Game machine presentation payload exceeds the entity limit");
    }
    return bytes;
}

snt::core::Expected<GameMachineReplicationEntity>
decode_game_machine_replication_entity(std::span<const std::byte> payload) {
    constexpr size_t kEntityHeaderBytes = sizeof(uint8_t) * 4;
    if (payload.size() < kEntityHeaderBytes ||
        !is_game_machine_replication_entity_payload(payload) ||
        std::to_integer<uint8_t>(payload[1]) != kGameMachineReplicationEntityVersion ||
        std::to_integer<uint8_t>(payload[3]) != 0) {
        return protocol_error("Game machine replication entity header is invalid");
    }
    const auto operation = static_cast<GameMachineReplicationOperation>(
        std::to_integer<uint8_t>(payload[2]));
    if (!is_valid_machine_operation(operation)) {
        return protocol_error("Game machine replication operation is invalid");
    }
    if (operation == GameMachineReplicationOperation::kRemove) {
        if (payload.size() != kEntityHeaderBytes) {
            return protocol_error("Game machine remove has trailing bytes");
        }
        return GameMachineReplicationEntity{.operation = operation, .machine = std::nullopt};
    }

    size_t offset = kEntityHeaderBytes;
    auto anchor_chunk = read_chunk_key(payload, offset);
    if (!anchor_chunk) return anchor_chunk.error();
    if (payload.size() - offset < sizeof(uint32_t) * 3 + sizeof(uint16_t)) {
        return protocol_error("Game machine replication entity is truncated");
    }
    GameReplicatedMachineState machine;
    machine.anchor_chunk = std::move(*anchor_chunk);
    machine.root_x = read_i32(payload, offset);
    machine.root_y = read_i32(payload, offset + sizeof(uint32_t));
    machine.root_z = read_i32(payload, offset + sizeof(uint32_t) * 2);
    offset += sizeof(uint32_t) * 3;
    auto machine_id = read_short_string(payload, offset, kMaxReplicatedMachineIdBytes,
                                        "id", true);
    if (!machine_id) return machine_id.error();
    machine.machine_id = std::move(*machine_id);
    if (payload.size() - offset < sizeof(uint8_t) * 4 + sizeof(uint32_t) * 4) {
        return protocol_error("Game machine replication entity state is truncated");
    }
    machine.run_state = std::to_integer<uint8_t>(payload[offset]);
    machine.max_input_slots = std::to_integer<uint8_t>(payload[offset + 1]);
    machine.max_output_slots = std::to_integer<uint8_t>(payload[offset + 2]);
    if (std::to_integer<uint8_t>(payload[offset + 3]) != 0) {
        return protocol_error("Game machine replication entity reserved bytes are invalid");
    }
    offset += sizeof(uint8_t) * 4;
    machine.stored_energy = read_i32(payload, offset);
    machine.energy_capacity = read_i32(payload, offset + sizeof(uint32_t));
    machine.progress_ticks = read_i32(payload, offset + sizeof(uint32_t) * 2);
    machine.active_recipe_duration_ticks =
        read_i32(payload, offset + sizeof(uint32_t) * 3);
    offset += sizeof(uint32_t) * 4;
    auto inputs = read_machine_stacks(payload, offset, "input slots");
    if (!inputs) return inputs.error();
    auto outputs = read_machine_stacks(payload, offset, "output slots");
    if (!outputs) return outputs.error();
    machine.input_slots = std::move(*inputs);
    machine.output_slots = std::move(*outputs);
    if (offset != payload.size()) {
        return protocol_error("Game machine replication entity has trailing bytes");
    }
    if (auto result = validate_machine_state(machine); !result) return result.error();
    return GameMachineReplicationEntity{.operation = operation, .machine = std::move(machine)};
}

GameClientRemoteChunkWorld::GameClientRemoteChunkWorld(
    snt::voxel::ChunkRegistry& chunks) noexcept
    : chunks_(&chunks) {}

snt::core::Expected<void> GameClientRemoteChunkWorld::apply(const GameSnapshot& snapshot) {
    if (chunks_ == nullptr) return invalid_state("Client remote chunk world is unavailable");
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Client remote chunk world received an invalid snapshot id");
    }

    std::set<snt::voxel::ChunkKey, GameChunkKeyLess> seen;
    for (const GameChunkSnapshot& chunk : snapshot.chunks) {
        if (!seen.insert(chunk.chunk).second) {
            return protocol_error("Client remote chunk snapshot has a duplicate chunk");
        }
        auto decoded = decode_game_terrain_chunk_snapshot(chunk.payload);
        if (!decoded) return decoded.error();
    }

    clear();
    for (const GameChunkSnapshot& chunk : snapshot.chunks) {
        if (auto result = apply_chunk_snapshot(chunk); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameClientRemoteChunkWorld::apply(const GameDelta& delta) {
    if (chunks_ == nullptr) return invalid_state("Client remote chunk world is unavailable");
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Client remote chunk delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Client remote chunk delta sequence is invalid");
    }

    for (const GameChunkSnapshot& chunk : delta.chunk_snapshots) {
        auto decoded = decode_game_terrain_chunk_snapshot(chunk.payload);
        if (!decoded) return decoded.error();
    }
    for (const GameChunkDelta& chunk_delta : delta.chunks) {
        if (!active_chunks_.contains(chunk_delta.chunk)) {
            return protocol_error("Client remote chunk delta targets an unknown chunk");
        }
        const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            chunk_delta.chunk.dimension_id, chunk_delta.chunk.chunk_x,
            chunk_delta.chunk.chunk_y, chunk_delta.chunk.chunk_z);
        if (chunk == nullptr) {
            return protocol_error("Client remote chunk delta target is absent from the chunk registry");
        }
        for (const GameBlockDelta& block : chunk_delta.blocks) {
            if (block.local_index >= chunk->terrain.cells.size()) {
                return protocol_error("Client remote chunk delta has an invalid local cell index");
            }
        }
    }

    for (const snt::voxel::ChunkKey& key : delta.removed_chunks) {
        if (auto result = restore_chunk(key); !result) return result.error();
    }
    for (const GameChunkSnapshot& chunk : delta.chunk_snapshots) {
        if (auto result = apply_chunk_snapshot(chunk); !result) return result.error();
    }
    for (const GameChunkDelta& chunk_delta : delta.chunks) {
        snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            chunk_delta.chunk.dimension_id, chunk_delta.chunk.chunk_x,
            chunk_delta.chunk.chunk_y, chunk_delta.chunk.chunk_z);
        if (chunk == nullptr) {
            return invalid_state("Client remote chunk delta target disappeared during application");
        }
        for (const GameBlockDelta& block : chunk_delta.blocks) {
            snt::voxel::TerrainCell& cell = chunk->terrain.cells[block.local_index];
            cell.material = block.material;
            cell.flags = block.flags;
        }
        mark_dirty(chunk_delta.chunk);
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::vector<snt::voxel::ChunkKey> GameClientRemoteChunkWorld::drain_dirty_chunks() {
    std::vector<snt::voxel::ChunkKey> result;
    result.reserve(dirty_chunks_.size());
    for (const auto& [key, value] : dirty_chunks_) {
        static_cast<void>(value);
        result.push_back(key);
    }
    dirty_chunks_.clear();
    return result;
}

void GameClientRemoteChunkWorld::clear() noexcept {
    if (chunks_ != nullptr) {
        for (const auto& [key, original] : original_chunks_) {
            if (original.has_value()) {
                chunks_->set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z,
                                   *original);
            } else {
                chunks_->remove_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
            }
            mark_dirty(key);
        }
    }
    active_chunks_.clear();
    original_chunks_.clear();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameClientRemoteChunkWorld::apply_chunk_snapshot(
    const GameChunkSnapshot& snapshot) {
    if (chunks_ == nullptr) return invalid_state("Client remote chunk world is unavailable");
    auto terrain = decode_game_terrain_chunk_snapshot(snapshot.payload);
    if (!terrain) return terrain.error();
    auto voxel_chunk = make_voxel_chunk_from_game_replicated_terrain(snapshot.chunk, *terrain);
    if (!voxel_chunk) return voxel_chunk.error();
    if (!original_chunks_.contains(snapshot.chunk)) {
        const snt::voxel::VoxelChunk* existing = chunks_->get_chunk(
            snapshot.chunk.dimension_id, snapshot.chunk.chunk_x,
            snapshot.chunk.chunk_y, snapshot.chunk.chunk_z);
        if (existing != nullptr) {
            original_chunks_.emplace(snapshot.chunk, *existing);
        } else {
            original_chunks_.emplace(snapshot.chunk, std::nullopt);
        }
    }
    chunks_->set_chunk(snapshot.chunk.dimension_id, snapshot.chunk.chunk_x,
                       snapshot.chunk.chunk_y, snapshot.chunk.chunk_z,
                       std::move(*voxel_chunk));
    active_chunks_.insert_or_assign(snapshot.chunk, true);
    mark_dirty(snapshot.chunk);
    return {};
}

snt::core::Expected<void> GameClientRemoteChunkWorld::restore_chunk(
    const snt::voxel::ChunkKey& key) {
    if (chunks_ == nullptr) return invalid_state("Client remote chunk world is unavailable");
    const auto original = original_chunks_.find(key);
    if (original == original_chunks_.end()) {
        if (!active_chunks_.contains(key)) {
            return protocol_error("Client remote chunk removal targets an unknown chunk");
        }
        return invalid_state("Client remote chunk has no restoration baseline");
    }
    if (original->second.has_value()) {
        chunks_->set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z,
                           *original->second);
    } else {
        chunks_->remove_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    }
    active_chunks_.erase(key);
    original_chunks_.erase(original);
    mark_dirty(key);
    return {};
}

void GameClientRemoteChunkWorld::mark_dirty(const snt::voxel::ChunkKey& key) {
    dirty_chunks_.insert_or_assign(key, true);
}

snt::core::Expected<void> GameRemoteMachineWorld::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Remote machine world received an invalid snapshot id");
    }
    std::map<uint64_t, GameRemoteMachineState> next;
    for (const GameEntitySnapshot& entity : snapshot.entities) {
        if (!is_game_machine_replication_entity_payload(entity.payload)) continue;
        if (!entity.entity_guid.valid()) {
            return protocol_error("Remote machine snapshot has an invalid entity guid");
        }
        auto decoded = decode_game_machine_replication_entity(entity.payload);
        if (!decoded) return decoded.error();
        if (decoded->operation != GameMachineReplicationOperation::kUpsert ||
            !decoded->machine.has_value() ||
            !next.emplace(entity.entity_guid.value,
                          GameRemoteMachineState{.entity_guid = entity.entity_guid,
                                                 .machine = std::move(*decoded->machine)})
                 .second) {
            return protocol_error("Remote machine snapshot has an invalid machine entity");
        }
    }
    machines_ = std::move(next);
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemoteMachineWorld::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Remote machine delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Remote machine delta sequence is invalid");
    }
    for (const GameEntitySnapshot& entity : delta.entities) {
        if (!is_game_machine_replication_entity_payload(entity.payload)) continue;
        auto decoded = decode_game_machine_replication_entity(entity.payload);
        if (!decoded) return decoded.error();
        if (auto result = apply_entity(entity.entity_guid, *decoded); !result) return result.error();
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::vector<GameRemoteMachineState> GameRemoteMachineWorld::machines() const {
    std::vector<GameRemoteMachineState> result;
    result.reserve(machines_.size());
    for (const auto& [guid, machine] : machines_) {
        static_cast<void>(guid);
        result.push_back(machine);
    }
    return result;
}

std::optional<GameRemoteMachineState> GameRemoteMachineWorld::find_machine_at(
    std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const {
    const auto found = std::find_if(
        machines_.begin(), machines_.end(),
        [dimension_id, root_x, root_y, root_z](const auto& entry) {
            const GameReplicatedMachineState& machine = entry.second.machine;
            return machine.anchor_chunk.dimension_id == dimension_id &&
                   machine.root_x == root_x && machine.root_y == root_y &&
                   machine.root_z == root_z;
        });
    if (found == machines_.end()) return std::nullopt;
    return found->second;
}

void GameRemoteMachineWorld::clear() noexcept {
    machines_.clear();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemoteMachineWorld::apply_entity(
    snt::ecs::EntityGuid entity_guid, const GameMachineReplicationEntity& entity) {
    if (!entity_guid.valid()) {
        return protocol_error("Remote machine delta has an invalid entity guid");
    }
    switch (entity.operation) {
        case GameMachineReplicationOperation::kUpsert:
            if (!entity.machine.has_value()) {
                return protocol_error("Remote machine upsert has no state");
            }
            machines_.insert_or_assign(entity_guid.value,
                                       GameRemoteMachineState{.entity_guid = entity_guid,
                                                              .machine = *entity.machine});
            return {};
        case GameMachineReplicationOperation::kRemove:
            if (entity.machine.has_value()) {
                return protocol_error("Remote machine remove carries state");
            }
            machines_.erase(entity_guid.value);
            return {};
    }
    return protocol_error("Remote machine delta operation is invalid");
}

}  // namespace snt::game::replication
