// Cable-backed offline power island implementation.

#define SNT_LOG_CHANNEL "game.offline_power_island"
#include "game/simulation/offline_power_network_island.h"

#include "core/error.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/world/defs/block_entity.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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

struct ChunkKeyLess {
    bool operator()(const ChunkKey& left, const ChunkKey& right) const noexcept {
        return chunk_key_less(left, right);
    }
};

struct WorldPosition {
    std::string dimension_id;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

struct WorldPositionLess {
    bool operator()(const WorldPosition& left, const WorldPosition& right) const noexcept {
        if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
        if (left.x != right.x) return left.x < right.x;
        if (left.y != right.y) return left.y < right.y;
        return left.z < right.z;
    }
};

[[nodiscard]] bool same_position(const WorldPosition& left,
                                 const WorldPosition& right) noexcept {
    return left.dimension_id == right.dimension_id && left.x == right.x &&
           left.y == right.y && left.z == right.z;
}

[[nodiscard]] int32_t chunk_coordinate_for_block(int32_t block_coordinate) noexcept {
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t value = block_coordinate;
    return static_cast<int32_t>(value >= 0
        ? value / kChunkSize
        : -(((-value) + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] ChunkKey chunk_for_position(const WorldPosition& position) {
    return {
        position.dimension_id,
        chunk_coordinate_for_block(position.x),
        chunk_coordinate_for_block(position.y),
        chunk_coordinate_for_block(position.z),
    };
}

[[nodiscard]] bool parse_int(std::string_view value, int& out) noexcept {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

struct ParsedCable {
    VoltageTier tier = VoltageTier::ULV;
    uint8_t connections = 0;
};

[[nodiscard]] snt::core::Expected<ParsedCable> parse_cable(
    const BlockEntityPlacement& placement) {
    const size_t separator = placement.type_data_json.find('|');
    if (separator == std::string::npos ||
        placement.type_data_json.find('|', separator + 1) != std::string::npos) {
        return invalid_argument("Cable placement must encode tier|connection_mask");
    }
    int raw_tier = 0;
    int raw_connections = 0;
    const std::string_view text(placement.type_data_json);
    if (!parse_int(text.substr(0, separator), raw_tier) ||
        !parse_int(text.substr(separator + 1), raw_connections) ||
        raw_tier < 0 || raw_tier > static_cast<int>(VoltageTier::MAX) ||
        raw_connections < 0 || raw_connections > 0x3f) {
        return invalid_argument("Cable placement has an invalid tier or connection mask");
    }
    return ParsedCable{
        .tier = static_cast<VoltageTier>(raw_tier),
        .connections = static_cast<uint8_t>(raw_connections),
    };
}

class DisjointSets final {
public:
    explicit DisjointSets(size_t count) : parents_(count), ranks_(count, 0) {
        for (size_t index = 0; index < count; ++index) parents_[index] = index;
    }

    size_t find(size_t node) {
        if (parents_[node] != node) parents_[node] = find(parents_[node]);
        return parents_[node];
    }

    void join(size_t left, size_t right) {
        left = find(left);
        right = find(right);
        if (left == right) return;
        if (ranks_[left] < ranks_[right]) std::swap(left, right);
        parents_[right] = left;
        if (ranks_[left] == ranks_[right]) ++ranks_[left];
    }

private:
    std::vector<size_t> parents_;
    std::vector<uint8_t> ranks_;
};

enum class PowerGraphNodeKind : uint8_t {
    kCable = 0,
    kMachine = 1,
};

struct PowerGraphNode {
    PowerGraphNodeKind kind = PowerGraphNodeKind::kCable;
    WorldPosition position;
    ChunkKey chunk_key;
    uint64_t stable_id = 0;
    uint8_t cable_connections = 0;
    VoltageTier cable_tier = VoltageTier::ULV;
    uint64_t machine_guid = 0;
    int32_t machine_energy_capacity = 0;
};

[[nodiscard]] bool power_graph_node_less(const PowerGraphNode& left,
                                         const PowerGraphNode& right) noexcept {
    const WorldPositionLess position_less;
    if (position_less(left.position, right.position)) return true;
    if (position_less(right.position, left.position)) return false;
    if (left.kind != right.kind) {
        return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    }
    return left.stable_id < right.stable_id;
}

constexpr std::array<int32_t, 6> kDirectionX{1, -1, 0, 0, 0, 0};
constexpr std::array<int32_t, 6> kDirectionY{0, 0, 1, -1, 0, 0};
constexpr std::array<int32_t, 6> kDirectionZ{0, 0, 0, 0, 1, -1};
constexpr std::array<uint8_t, 6> kOppositeDirection{1, 0, 3, 2, 5, 4};

[[nodiscard]] WorldPosition step_position(const WorldPosition& position,
                                          uint8_t direction) {
    return {
        position.dimension_id,
        static_cast<int32_t>(position.x + kDirectionX[direction]),
        static_cast<int32_t>(position.y + kDirectionY[direction]),
        static_cast<int32_t>(position.z + kDirectionZ[direction]),
    };
}

void hash_byte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ull;
}

void hash_uint64(uint64_t& hash, uint64_t value) noexcept {
    for (uint8_t index = 0; index < 8; ++index) {
        hash_byte(hash, static_cast<uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

void hash_string(uint64_t& hash, std::string_view value) noexcept {
    for (const char character : value) hash_byte(hash, static_cast<uint8_t>(character));
    hash_byte(hash, 0xffu);
}

[[nodiscard]] uint64_t topology_revision_for(
    const std::vector<size_t>& node_indices,
    const std::vector<PowerGraphNode>& nodes) noexcept {
    uint64_t hash = 1469598103934665603ull;
    for (const size_t index : node_indices) {
        const PowerGraphNode& node = nodes[index];
        hash_byte(hash, static_cast<uint8_t>(node.kind));
        hash_string(hash, node.position.dimension_id);
        hash_uint64(hash, static_cast<uint32_t>(node.position.x));
        hash_uint64(hash, static_cast<uint32_t>(node.position.y));
        hash_uint64(hash, static_cast<uint32_t>(node.position.z));
        hash_uint64(hash, node.stable_id);
        hash_byte(hash, node.cable_connections);
        hash_byte(hash, static_cast<uint8_t>(node.cable_tier));
    }
    return hash == 0 ? 1 : hash;
}

[[nodiscard]] const BlockEntityPlacement* find_machine_anchor(
    const GameChunkSidecar& sidecar,
    EntityId anchor_id) {
    const auto found = std::find_if(
        sidecar.block_entities.begin(), sidecar.block_entities.end(),
        [anchor_id](const BlockEntityPlacement& placement) {
            return placement.id == anchor_id && placement.entity_type == BlockEntityType::MACHINE;
        });
    return found == sidecar.block_entities.end() ? nullptr : &*found;
}

struct PowerMachineRecord {
    MachineRuntimePersistenceRecord* record = nullptr;
    const MachineDefinition* definition = nullptr;
};

[[nodiscard]] snt::core::Expected<std::vector<PowerMachineRecord>> find_power_records(
    OfflineNetworkIslandSnapshot& snapshot,
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars) {
    std::vector<PowerMachineRecord> result;
    result.reserve(snapshot.machine_guids.size());
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        MachineRuntimePersistenceRecord* found_record = nullptr;
        for (const ChunkKey& chunk_key : snapshot.member_chunks) {
            GameChunkSidecar* sidecar = sidecars.get(chunk_key);
            if (sidecar == nullptr) continue;
            for (MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
                if (record.entity_guid != entity_guid) continue;
                if (found_record != nullptr) {
                    return invalid_state("Offline power island has duplicate machine records");
                }
                found_record = &record;
            }
        }
        if (found_record == nullptr ||
            found_record->residency != MachineRuntimeResidency::kOfflineNetworkIsland ||
            found_record->offline_island_id != snapshot.island_id ||
            found_record->offline_epoch != snapshot.ownership_epoch) {
            return invalid_state("Offline power island machine ownership is invalid");
        }
        const MachineDefinition* definition = content.find_machine(found_record->machine_id);
        if (definition == nullptr ||
            definition->offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland) {
            return invalid_state("Offline power island machine content profile is unavailable");
        }
        result.push_back({found_record, definition});
    }
    return result;
}

}  // namespace

OfflinePowerNetworkIslandProvider::OfflinePowerNetworkIslandProvider(
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars) noexcept
    : content_(&content), sidecars_(&sidecars) {}

snt::core::Expected<std::vector<OfflineNetworkIslandSnapshot>>
OfflinePowerNetworkIslandProvider::build_offline_islands(
    std::span<const ChunkKey> candidate_chunks,
    uint64_t source_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline power topology provider is unavailable");
    }
    std::set<ChunkKey, ChunkKeyLess> candidates;
    for (const ChunkKey& chunk_key : candidate_chunks) {
        if (sidecars_->get(chunk_key) == nullptr || !candidates.insert(chunk_key).second) {
            return invalid_argument("Offline power topology candidates must be unique loaded chunks");
        }
    }

    std::vector<PowerGraphNode> nodes;
    std::optional<snt::core::Error> error;
    sidecars_->for_each([&](const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
        if (error.has_value()) return;
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            if (placement.entity_type != BlockEntityType::CABLE) continue;
            auto cable = parse_cable(placement);
            if (!cable || placement.id.id == 0) {
                error = cable ? invalid_state("Cable placement has an invalid entity id")
                              : cable.error();
                return;
            }
            nodes.push_back({
                .kind = PowerGraphNodeKind::kCable,
                .position = {chunk_key.dimension_id, placement.root_x, placement.root_y,
                             placement.root_z},
                .chunk_key = chunk_key,
                .stable_id = placement.id.id,
                .cable_connections = cable->connections,
                .cable_tier = cable->tier,
            });
        }
        for (const MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (record.residency != MachineRuntimeResidency::kLoaded) continue;
            const MachineDefinition* definition = content_->find_machine(record.machine_id);
            if (definition == nullptr ||
                definition->offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland) {
                continue;
            }
            const BlockEntityPlacement* anchor = find_machine_anchor(sidecar, record.anchor_entity_id);
            if (anchor == nullptr) {
                error = invalid_state("Offline power topology machine anchor is unavailable");
                return;
            }
            nodes.push_back({
                .kind = PowerGraphNodeKind::kMachine,
                .position = {chunk_key.dimension_id, anchor->root_x, anchor->root_y,
                             anchor->root_z},
                .chunk_key = chunk_key,
                .stable_id = record.entity_guid,
                .machine_guid = record.entity_guid,
                .machine_energy_capacity = record.energy_capacity,
            });
        }
    });
    if (error.has_value()) return *error;
    if (nodes.empty()) return std::vector<OfflineNetworkIslandSnapshot>{};

    std::sort(nodes.begin(), nodes.end(), power_graph_node_less);
    std::map<WorldPosition, size_t, WorldPositionLess> cables_by_position;
    std::map<WorldPosition, size_t, WorldPositionLess> machines_by_position;
    for (size_t index = 0; index < nodes.size(); ++index) {
        const PowerGraphNode& node = nodes[index];
        auto& positions = node.kind == PowerGraphNodeKind::kCable
            ? cables_by_position
            : machines_by_position;
        if (!positions.emplace(node.position, index).second) {
            return invalid_state("Offline power topology has duplicate cable or machine positions");
        }
    }

    DisjointSets graph(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        const PowerGraphNode& cable = nodes[index];
        if (cable.kind != PowerGraphNodeKind::kCable) continue;
        for (uint8_t direction = 0; direction < 6; ++direction) {
            if ((cable.cable_connections & (uint8_t{1} << direction)) == 0) continue;
            const WorldPosition neighbor = step_position(cable.position, direction);
            if (const auto other_cable = cables_by_position.find(neighbor);
                other_cable != cables_by_position.end()) {
                const PowerGraphNode& other = nodes[other_cable->second];
                if ((other.cable_connections &
                     (uint8_t{1} << kOppositeDirection[direction])) != 0) {
                    graph.join(index, other_cable->second);
                }
                continue;
            }
            if (const auto machine = machines_by_position.find(neighbor);
                machine != machines_by_position.end()) {
                graph.join(index, machine->second);
            }
        }
    }

    std::map<size_t, std::vector<size_t>> components;
    for (size_t index = 0; index < nodes.size(); ++index) {
        components[graph.find(index)].push_back(index);
    }

    std::vector<OfflineNetworkIslandSnapshot> snapshots;
    for (const auto& [root, component] : components) {
        static_cast<void>(root);
        bool has_candidate_machine = false;
        bool all_nodes_are_candidates = true;
        std::vector<ChunkKey> member_chunks;
        std::vector<uint64_t> machine_guids;
        int64_t power_capacity = 0;
        int64_t power_transfer_per_tick = std::numeric_limits<int64_t>::max();
        bool has_cable = false;
        std::string dimension_id;
        for (const size_t index : component) {
            const PowerGraphNode& node = nodes[index];
            if (!candidates.contains(node.chunk_key)) all_nodes_are_candidates = false;
            member_chunks.push_back(node.chunk_key);
            if (node.kind == PowerGraphNodeKind::kCable) {
                has_cable = true;
                power_transfer_per_tick = std::min(
                    power_transfer_per_tick, get_voltage(node.cable_tier));
            }
            if (node.kind != PowerGraphNodeKind::kMachine) continue;
            has_candidate_machine = has_candidate_machine || candidates.contains(node.chunk_key);
            machine_guids.push_back(node.machine_guid);
            dimension_id = node.position.dimension_id;
            const int64_t capacity = std::max<int64_t>(node.machine_energy_capacity, 0);
            power_capacity = capacity > std::numeric_limits<int64_t>::max() - power_capacity
                ? std::numeric_limits<int64_t>::max()
                : power_capacity + capacity;
        }
        if (!has_candidate_machine || !all_nodes_are_candidates) continue;
        std::sort(member_chunks.begin(), member_chunks.end(), chunk_key_less);
        member_chunks.erase(std::unique(member_chunks.begin(), member_chunks.end(), same_chunk),
                            member_chunks.end());
        std::sort(machine_guids.begin(), machine_guids.end());
        if (machine_guids.empty() || machine_guids.front() == 0) {
            return invalid_state("Offline power topology component has invalid machine identity");
        }
        snapshots.push_back({
            .island_id = machine_guids.front(),
            .dimension_id = dimension_id,
            .anchor_chunk = member_chunks.front(),
            .member_chunks = std::move(member_chunks),
            .topology_revision = topology_revision_for(component, nodes),
            .last_simulated_tick = source_tick,
            .machine_guids = std::move(machine_guids),
            .ledgers = {{
                .kind = OfflineNetworkResourceKind::kPower,
                .resource_id = std::string(kOfflinePowerLedgerResourceId),
                .stored_amount = 0,
                .capacity = power_capacity,
                .max_transfer_per_tick = has_cable ? power_transfer_per_tick : 0,
            }},
        });
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const OfflineNetworkIslandSnapshot& left,
                 const OfflineNetworkIslandSnapshot& right) {
                  return left.island_id < right.island_id;
              });
    return snapshots;
}

snt::core::Expected<uint64_t> OfflinePowerNetworkIslandSimulator::advance_offline_island(
    OfflineNetworkIslandSnapshot& snapshot,
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars,
    IMachineTickEventSink* event_sink,
    uint64_t first_tick,
    uint64_t tick_count) {
    const auto ledger_it = std::find_if(
        snapshot.ledgers.begin(), snapshot.ledgers.end(),
        [](const OfflineNetworkResourceLedger& ledger) {
            return ledger.kind == OfflineNetworkResourceKind::kPower &&
                   ledger.resource_id == kOfflinePowerLedgerResourceId;
        });
    if (ledger_it == snapshot.ledgers.end()) {
        return invalid_state("Offline power island is missing its power ledger");
    }
    OfflineNetworkResourceLedger& ledger = *ledger_it;
    if (ledger.stored_amount < 0 || ledger.capacity < 0 ||
        ledger.max_transfer_per_tick < 0 || ledger.stored_amount > ledger.capacity) {
        return invalid_state("Offline power island ledger is invalid");
    }
    auto records = find_power_records(snapshot, content, sidecars);
    if (!records) return records.error();

    for (uint64_t offset = 0; offset < tick_count; ++offset) {
        // Explicit exports run first so a storage-only source can make power
        // available to a waiting consumer in this same deterministic tick.
        int64_t remaining_export = ledger.max_transfer_per_tick;
        for (const PowerMachineRecord& machine : *records) {
            const int64_t room = ledger.capacity - ledger.stored_amount;
            const int64_t transferred = std::min({
                room,
                remaining_export,
                static_cast<int64_t>(machine.definition->offline_simulation.max_power_export_per_tick),
                static_cast<int64_t>(machine.record->stored_energy),
            });
            if (transferred <= 0) continue;
            machine.record->stored_energy -= static_cast<int32_t>(transferred);
            ledger.stored_amount += transferred;
            remaining_export -= transferred;
        }
        int64_t remaining_import = ledger.max_transfer_per_tick;
        for (const PowerMachineRecord& machine : *records) {
            const int64_t room = std::max<int64_t>(
                0, static_cast<int64_t>(machine.record->energy_capacity) -
                       machine.record->stored_energy);
            const int64_t transferred = std::min({
                ledger.stored_amount,
                room,
                remaining_import,
                static_cast<int64_t>(machine.definition->offline_simulation.max_power_import_per_tick),
            });
            if (transferred <= 0) continue;
            machine.record->stored_energy += static_cast<int32_t>(transferred);
            ledger.stored_amount -= transferred;
            remaining_import -= transferred;
        }
        for (const PowerMachineRecord& machine : *records) {
            auto input = make_machine_execution_input(
                content, snt::ecs::EntityGuid{machine.record->entity_guid},
                GameMachineRuntimePersistence::make_runtime_component(*machine.record));
            if (!input) return input.error();
            input->allow_new_jobs =
                machine.definition->offline_simulation.can_start_new_jobs &&
                !machine.definition->requires_manual_activation;
            MachineExecutionResult result = advance_machine_execution(
                std::move(*input), first_tick + offset, 1);
            GameMachineRuntimePersistence::copy_runtime_to_record(
                *machine.record, result.machine);
            if (event_sink != nullptr) {
                for (const MachineTickEvent& event : result.events) {
                    event_sink->on_machine_tick_event(event);
                }
            }
        }
    }
    return tick_count;
}

}  // namespace snt::game
