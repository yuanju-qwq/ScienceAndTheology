// Offline industrial-network island implementation.

#define SNT_LOG_CHANNEL "game.offline_industrial_network"
#include "game/simulation/offline_industrial_network_island.h"

#include "core/error.h"
#include "core/log.h"
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

[[nodiscard]] bool parse_int(std::string_view value, int& out) noexcept {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

struct ParsedTransportPlacement {
    int raw_type = 0;
    uint8_t connections = 0;
};

[[nodiscard]] snt::core::Expected<ParsedTransportPlacement> parse_transport_placement(
    const BlockEntityPlacement& placement,
    std::string_view transport_name) {
    const size_t separator = placement.type_data_json.find('|');
    if (separator == std::string::npos ||
        placement.type_data_json.find('|', separator + 1) != std::string::npos) {
        return invalid_argument(std::string(transport_name) +
                                " placement must encode type|connection_mask");
    }
    int raw_type = 0;
    int raw_connections = 0;
    const std::string_view text(placement.type_data_json);
    if (!parse_int(text.substr(0, separator), raw_type) ||
        !parse_int(text.substr(separator + 1), raw_connections) ||
        raw_connections < 0 || raw_connections > 0x3f) {
        return invalid_argument(std::string(transport_name) +
                                " placement has an invalid type or connection mask");
    }
    return ParsedTransportPlacement{
        .raw_type = raw_type,
        .connections = static_cast<uint8_t>(raw_connections),
    };
}

struct ParsedCable {
    VoltageTier tier = VoltageTier::ULV;
    uint8_t connections = 0;
};

[[nodiscard]] snt::core::Expected<ParsedCable> parse_cable(
    const BlockEntityPlacement& placement) {
    auto parsed = parse_transport_placement(placement, "Cable");
    if (!parsed) return parsed.error();
    if (parsed->raw_type < 0 ||
        parsed->raw_type > static_cast<int>(VoltageTier::MAX)) {
        return invalid_argument("Cable placement has an invalid voltage tier");
    }
    return ParsedCable{
        .tier = static_cast<VoltageTier>(parsed->raw_type),
        .connections = parsed->connections,
    };
}

struct ParsedPipe {
    PipeType type = PipeType::LIQUID;
    uint8_t connections = 0;
};

[[nodiscard]] snt::core::Expected<ParsedPipe> parse_pipe(
    const BlockEntityPlacement& placement) {
    auto parsed = parse_transport_placement(placement, "Pipe");
    if (!parsed) return parsed.error();
    if (parsed->raw_type < static_cast<int>(PipeType::LIQUID) ||
        parsed->raw_type > static_cast<int>(PipeType::ITEM)) {
        return invalid_argument("Pipe placement has an invalid pipe type");
    }
    return ParsedPipe{
        .type = static_cast<PipeType>(parsed->raw_type),
        .connections = parsed->connections,
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

enum class IndustrialGraphNodeKind : uint8_t {
    kCable = 0,
    kPipe = 1,
    kMachine = 2,
};

struct IndustrialGraphNode {
    IndustrialGraphNodeKind kind = IndustrialGraphNodeKind::kCable;
    WorldPosition position;
    ChunkKey chunk_key;
    uint64_t stable_id = 0;
    uint8_t connections = 0;
    VoltageTier cable_tier = VoltageTier::ULV;
    PipeType pipe_type = PipeType::LIQUID;
    uint64_t machine_guid = 0;
    int32_t machine_energy_capacity = 0;
};

[[nodiscard]] bool industrial_graph_node_less(const IndustrialGraphNode& left,
                                              const IndustrialGraphNode& right) noexcept {
    const WorldPositionLess position_less;
    if (position_less(left.position, right.position)) return true;
    if (position_less(right.position, left.position)) return false;
    if (left.kind != right.kind) {
        return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    }
    if (left.kind == IndustrialGraphNodeKind::kPipe && left.pipe_type != right.pipe_type) {
        return static_cast<uint8_t>(left.pipe_type) < static_cast<uint8_t>(right.pipe_type);
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

void hash_node(uint64_t& hash, const IndustrialGraphNode& node) noexcept {
    hash_byte(hash, static_cast<uint8_t>(node.kind));
    hash_string(hash, node.position.dimension_id);
    hash_uint64(hash, static_cast<uint32_t>(node.position.x));
    hash_uint64(hash, static_cast<uint32_t>(node.position.y));
    hash_uint64(hash, static_cast<uint32_t>(node.position.z));
    hash_uint64(hash, node.stable_id);
    hash_byte(hash, node.connections);
    if (node.kind == IndustrialGraphNodeKind::kCable) {
        hash_byte(hash, static_cast<uint8_t>(node.cable_tier));
    } else if (node.kind == IndustrialGraphNodeKind::kPipe) {
        hash_byte(hash, static_cast<uint8_t>(node.pipe_type));
    }
}

[[nodiscard]] uint64_t topology_revision_for(const std::vector<size_t>& node_indices,
                                              const std::vector<IndustrialGraphNode>& nodes) noexcept {
    uint64_t hash = 1469598103934665603ull;
    for (const size_t index : node_indices) hash_node(hash, nodes[index]);
    return hash == 0 ? 1 : hash;
}

[[nodiscard]] uint64_t segment_id_for(OfflineNetworkResourceKind kind,
                                      const std::vector<size_t>& node_indices,
                                      const std::vector<IndustrialGraphNode>& nodes) noexcept {
    uint64_t hash = 1469598103934665603ull;
    hash_byte(hash, static_cast<uint8_t>(kind));
    for (const size_t index : node_indices) hash_node(hash, nodes[index]);
    return hash == 0 ? 1 : hash;
}

[[nodiscard]] uint64_t reserve_segment_id(uint64_t proposed,
                                           std::set<uint64_t>& reserved) noexcept {
    uint64_t candidate = proposed == 0 ? 1 : proposed;
    while (!reserved.insert(candidate).second) {
        if (candidate == std::numeric_limits<uint64_t>::max()) return 0;
        ++candidate;
    }
    return candidate;
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

struct IndustrialMachineRecord {
    MachineRuntimePersistenceRecord* record = nullptr;
    const MachineDefinition* definition = nullptr;
};

[[nodiscard]] snt::core::Expected<std::vector<IndustrialMachineRecord>>
find_industrial_records(OfflineNetworkIslandSnapshot& snapshot,
                        GameContentRegistry& content,
                        GameChunkSidecarRegistry& sidecars) {
    std::vector<IndustrialMachineRecord> result;
    result.reserve(snapshot.machine_guids.size());
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        MachineRuntimePersistenceRecord* found_record = nullptr;
        for (const ChunkKey& chunk_key : snapshot.member_chunks) {
            GameChunkSidecar* sidecar = sidecars.get(chunk_key);
            if (sidecar == nullptr) continue;
            for (MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
                if (record.entity_guid != entity_guid) continue;
                if (found_record != nullptr) {
                    return invalid_state("Offline industrial island has duplicate machine records");
                }
                found_record = &record;
            }
        }
        if (found_record == nullptr ||
            found_record->residency != MachineRuntimeResidency::kOfflineNetworkIsland ||
            found_record->offline_island_id != snapshot.island_id ||
            found_record->offline_epoch != snapshot.ownership_epoch) {
            return invalid_state("Offline industrial island machine ownership is invalid");
        }
        const MachineDefinition* definition = content.find_machine(found_record->machine_id);
        if (definition == nullptr ||
            definition->offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland) {
            return invalid_state("Offline industrial island machine content profile is unavailable");
        }
        result.push_back({found_record, definition});
    }
    return result;
}

[[nodiscard]] snt::core::Expected<std::vector<IndustrialMachineRecord*>>
records_for_segment(const OfflineNetworkTransportSegment& segment,
                    std::vector<IndustrialMachineRecord>& records) {
    if (segment.segment_id == 0 || segment.capacity < 0 ||
        segment.max_transfer_per_tick < 0 || segment.machine_guids.empty()) {
        return invalid_state("Offline industrial transport segment is invalid");
    }
    std::vector<IndustrialMachineRecord*> result;
    result.reserve(segment.machine_guids.size());
    for (const uint64_t entity_guid : segment.machine_guids) {
        const auto found = std::find_if(
            records.begin(), records.end(), [entity_guid](const IndustrialMachineRecord& candidate) {
                return candidate.record != nullptr && candidate.record->entity_guid == entity_guid;
            });
        if (found == records.end()) {
            return invalid_state("Offline industrial transport segment references an unknown machine");
        }
        result.push_back(&*found);
    }
    return result;
}

[[nodiscard]] int64_t saturating_add_nonnegative(int64_t left, int64_t right) noexcept {
    if (right > std::numeric_limits<int64_t>::max() - left) {
        return std::numeric_limits<int64_t>::max();
    }
    return left + right;
}

[[nodiscard]] snt::core::Expected<void> transfer_power_segment(
    const OfflineNetworkTransportSegment& segment,
    OfflineNetworkResourceLedger& ledger,
    const std::vector<IndustrialMachineRecord*>& machines) {
    if (segment.kind != OfflineNetworkResourceKind::kPower ||
        ledger.segment_id != segment.segment_id ||
        ledger.kind != OfflineNetworkResourceKind::kPower ||
        ledger.resource != offline_network_power_ledger_key() ||
        ledger.capacity != segment.capacity ||
        ledger.max_transfer_per_tick != segment.max_transfer_per_tick ||
        ledger.stored_amount < 0 || ledger.capacity < 0 ||
        ledger.max_transfer_per_tick < 0 || ledger.stored_amount > ledger.capacity) {
        return invalid_state("Offline industrial power segment ledger is invalid");
    }

    int64_t remaining_export = ledger.max_transfer_per_tick;
    for (IndustrialMachineRecord* machine : machines) {
        if (machine == nullptr || machine->record == nullptr || machine->definition == nullptr ||
            machine->record->stored_energy < 0 || machine->record->energy_capacity < 0) {
            return invalid_state("Offline industrial power machine state is invalid");
        }
        const int64_t room = ledger.capacity - ledger.stored_amount;
        const int64_t transferred = std::min({
            room,
            remaining_export,
            static_cast<int64_t>(machine->definition->offline_simulation.max_power_export_per_tick),
            static_cast<int64_t>(machine->record->stored_energy),
        });
        if (transferred <= 0) continue;
        machine->record->stored_energy -= static_cast<int32_t>(transferred);
        ledger.stored_amount += transferred;
        remaining_export -= transferred;
    }

    int64_t remaining_import = ledger.max_transfer_per_tick;
    for (IndustrialMachineRecord* machine : machines) {
        if (machine == nullptr || machine->record == nullptr || machine->definition == nullptr ||
            machine->record->stored_energy < 0 || machine->record->energy_capacity < 0) {
            return invalid_state("Offline industrial power machine state is invalid");
        }
        const int64_t room = std::max<int64_t>(
            0, static_cast<int64_t>(machine->record->energy_capacity) -
                   machine->record->stored_energy);
        const int64_t transferred = std::min({
            ledger.stored_amount,
            room,
            remaining_import,
            static_cast<int64_t>(machine->definition->offline_simulation.max_power_import_per_tick),
        });
        if (transferred <= 0) continue;
        machine->record->stored_energy += static_cast<int32_t>(transferred);
        ledger.stored_amount -= transferred;
        remaining_import -= transferred;
    }
    return {};
}

[[nodiscard]] snt::core::Expected<int64_t> insert_item_into_inputs(
    MachineRuntimePersistenceRecord& target,
    const ResourceStack& source,
    int64_t requested_amount) {
    if (requested_amount <= 0) return int64_t{0};
    if (!source.is_valid() || !source.is_item() || target.max_input_slots <= 0 ||
        target.max_stack_size <= 0 ||
        target.input_slots.size() > static_cast<size_t>(target.max_input_slots)) {
        return invalid_state("Offline industrial item destination state is invalid");
    }

    int64_t remaining = requested_amount;
    for (MachineRuntimeItemStack& slot : target.input_slots) {
        if (!slot.resource.is_valid() || !slot.resource.is_item() ||
            slot.resource.amount > target.max_stack_size) {
            return invalid_state("Offline industrial item destination slot is invalid");
        }
        if (!slot.resource.has_same_key(source) || remaining == 0) continue;
        const int64_t room = static_cast<int64_t>(target.max_stack_size) - slot.resource.amount;
        const int64_t moved = std::min(room, remaining);
        slot.resource.amount += moved;
        remaining -= moved;
    }

    while (remaining != 0 &&
           target.input_slots.size() < static_cast<size_t>(target.max_input_slots)) {
        const int64_t moved = std::min<int64_t>(target.max_stack_size, remaining);
        target.input_slots.push_back({
            .resource = {
                .key = source.key,
                .amount = moved,
            },
        });
        remaining -= moved;
    }
    return requested_amount - remaining;
}

[[nodiscard]] snt::core::Expected<void> transfer_item_segment(
    const OfflineNetworkTransportSegment& segment,
    const std::vector<IndustrialMachineRecord*>& machines) {
    if (segment.kind != OfflineNetworkResourceKind::kItem ||
        segment.capacity < 0 || segment.max_transfer_per_tick < 0) {
        return invalid_state("Offline industrial item segment is invalid");
    }

    std::vector<int64_t> remaining_import;
    remaining_import.reserve(machines.size());
    for (IndustrialMachineRecord* machine : machines) {
        if (machine == nullptr || machine->record == nullptr || machine->definition == nullptr ||
            machine->definition->offline_simulation.max_item_import_per_tick < 0 ||
            machine->definition->offline_simulation.max_item_export_per_tick < 0) {
            return invalid_state("Offline industrial item machine profile is invalid");
        }
        remaining_import.push_back(
            static_cast<int64_t>(machine->definition->offline_simulation.max_item_import_per_tick));
    }

    int64_t remaining_segment_transfer = segment.max_transfer_per_tick;
    for (size_t source_index = 0;
         source_index < machines.size() && remaining_segment_transfer != 0;
         ++source_index) {
        IndustrialMachineRecord& source = *machines[source_index];
        int64_t remaining_export =
            static_cast<int64_t>(source.definition->offline_simulation.max_item_export_per_tick);
        size_t slot_index = 0;
        while (slot_index < source.record->output_slots.size() &&
               remaining_export != 0 && remaining_segment_transfer != 0) {
            MachineRuntimeItemStack& source_slot = source.record->output_slots[slot_index];
            if (!source_slot.resource.is_valid() || !source_slot.resource.is_item() ||
                source_slot.resource.amount > source.record->max_stack_size) {
                return invalid_state("Offline industrial item source slot is invalid");
            }

            for (size_t target_index = 0;
                 target_index < machines.size() && source_slot.resource.amount != 0 &&
                 remaining_export != 0 && remaining_segment_transfer != 0;
                 ++target_index) {
                if (target_index == source_index || remaining_import[target_index] == 0) continue;
                const int64_t request = std::min({
                    source_slot.resource.amount,
                    remaining_export,
                    remaining_import[target_index],
                    remaining_segment_transfer,
                });
                auto transferred = insert_item_into_inputs(
                    *machines[target_index]->record, source_slot.resource, request);
                if (!transferred) return transferred.error();
                if (*transferred == 0) continue;
                source_slot.resource.amount -= *transferred;
                remaining_export -= *transferred;
                remaining_import[target_index] -= *transferred;
                remaining_segment_transfer -= *transferred;
            }

            if (source_slot.resource.amount == 0) {
                source.record->output_slots.erase(
                    source.record->output_slots.begin() + static_cast<std::ptrdiff_t>(slot_index));
            } else {
                ++slot_index;
            }
        }
    }
    return {};
}

}  // namespace

OfflineIndustrialNetworkIslandProvider::OfflineIndustrialNetworkIslandProvider(
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars) noexcept
    : content_(&content), sidecars_(&sidecars) {}

snt::core::Expected<std::vector<OfflineNetworkIslandSnapshot>>
OfflineIndustrialNetworkIslandProvider::build_offline_islands(
    std::span<const ChunkKey> candidate_chunks,
    uint64_t source_tick) {
    if (content_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Offline industrial topology provider is unavailable");
    }
    std::set<ChunkKey, ChunkKeyLess> candidates;
    for (const ChunkKey& chunk_key : candidate_chunks) {
        if (sidecars_->get(chunk_key) == nullptr || !candidates.insert(chunk_key).second) {
            return invalid_argument(
                "Offline industrial topology candidates must be unique loaded chunks");
        }
    }

    std::vector<IndustrialGraphNode> nodes;
    std::set<uint64_t> machine_guids;
    std::optional<snt::core::Error> error;
    sidecars_->for_each([&](const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
        if (error.has_value()) return;
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            if (placement.entity_type == BlockEntityType::CABLE) {
                auto cable = parse_cable(placement);
                if (!cable || placement.id.id == 0) {
                    error = cable ? invalid_state("Cable placement has an invalid entity id")
                                  : cable.error();
                    return;
                }
                nodes.push_back({
                    .kind = IndustrialGraphNodeKind::kCable,
                    .position = {chunk_key.dimension_id, placement.root_x, placement.root_y,
                                 placement.root_z},
                    .chunk_key = chunk_key,
                    .stable_id = placement.id.id,
                    .connections = cable->connections,
                    .cable_tier = cable->tier,
                });
            } else if (placement.entity_type == BlockEntityType::PIPE) {
                auto pipe = parse_pipe(placement);
                if (!pipe || placement.id.id == 0) {
                    error = pipe ? invalid_state("Pipe placement has an invalid entity id")
                                 : pipe.error();
                    return;
                }
                nodes.push_back({
                    .kind = IndustrialGraphNodeKind::kPipe,
                    .position = {chunk_key.dimension_id, placement.root_x, placement.root_y,
                                 placement.root_z},
                    .chunk_key = chunk_key,
                    .stable_id = placement.id.id,
                    .connections = pipe->connections,
                    .pipe_type = pipe->type,
                });
            }
        }
        for (const MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (record.residency != MachineRuntimeResidency::kLoaded) continue;
            const MachineDefinition* definition = content_->find_machine(record.machine_id);
            if (definition == nullptr ||
                definition->offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland) {
                continue;
            }
            const BlockEntityPlacement* anchor = find_machine_anchor(sidecar, record.anchor_entity_id);
            if (anchor == nullptr || record.entity_guid == 0 ||
                !machine_guids.insert(record.entity_guid).second) {
                error = invalid_state("Offline industrial topology machine anchor or identity is invalid");
                return;
            }
            nodes.push_back({
                .kind = IndustrialGraphNodeKind::kMachine,
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

    std::sort(nodes.begin(), nodes.end(), industrial_graph_node_less);
    using PositionIndex = std::map<WorldPosition, size_t, WorldPositionLess>;
    PositionIndex cables_by_position;
    std::array<PositionIndex, 3> pipes_by_type;
    PositionIndex machines_by_position;
    for (size_t index = 0; index < nodes.size(); ++index) {
        const IndustrialGraphNode& node = nodes[index];
        PositionIndex* positions = nullptr;
        if (node.kind == IndustrialGraphNodeKind::kCable) {
            positions = &cables_by_position;
        } else if (node.kind == IndustrialGraphNodeKind::kPipe) {
            positions = &pipes_by_type[static_cast<size_t>(node.pipe_type)];
        } else {
            positions = &machines_by_position;
        }
        if (!positions->emplace(node.position, index).second) {
            return invalid_state("Offline industrial topology has duplicate network node positions");
        }
    }

    DisjointSets island_graph(nodes.size());
    DisjointSets power_graph(nodes.size());
    DisjointSets item_graph(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        const IndustrialGraphNode& node = nodes[index];
        if (node.kind == IndustrialGraphNodeKind::kMachine) continue;
        for (uint8_t direction = 0; direction < 6; ++direction) {
            if ((node.connections & (uint8_t{1} << direction)) == 0) continue;
            const WorldPosition neighbor = step_position(node.position, direction);
            if (node.kind == IndustrialGraphNodeKind::kCable) {
                if (const auto other_cable = cables_by_position.find(neighbor);
                    other_cable != cables_by_position.end()) {
                    const IndustrialGraphNode& other = nodes[other_cable->second];
                    if ((other.connections &
                         (uint8_t{1} << kOppositeDirection[direction])) != 0) {
                        island_graph.join(index, other_cable->second);
                        power_graph.join(index, other_cable->second);
                    }
                    continue;
                }
                if (const auto machine = machines_by_position.find(neighbor);
                    machine != machines_by_position.end()) {
                    island_graph.join(index, machine->second);
                    power_graph.join(index, machine->second);
                }
                continue;
            }

            PositionIndex& same_type_pipes =
                pipes_by_type[static_cast<size_t>(node.pipe_type)];
            if (const auto other_pipe = same_type_pipes.find(neighbor);
                other_pipe != same_type_pipes.end()) {
                const IndustrialGraphNode& other = nodes[other_pipe->second];
                if ((other.connections &
                     (uint8_t{1} << kOppositeDirection[direction])) != 0) {
                    island_graph.join(index, other_pipe->second);
                    if (node.pipe_type == PipeType::ITEM) {
                        item_graph.join(index, other_pipe->second);
                    }
                }
                continue;
            }
            if (const auto machine = machines_by_position.find(neighbor);
                machine != machines_by_position.end()) {
                island_graph.join(index, machine->second);
                if (node.pipe_type == PipeType::ITEM) {
                    item_graph.join(index, machine->second);
                }
            }
        }
    }

    std::map<size_t, std::vector<size_t>> island_components;
    for (size_t index = 0; index < nodes.size(); ++index) {
        island_components[island_graph.find(index)].push_back(index);
    }

    std::vector<OfflineNetworkIslandSnapshot> snapshots;
    size_t fluid_deferred_components = 0;
    for (const auto& [root, component] : island_components) {
        static_cast<void>(root);
        bool has_candidate_machine = false;
        bool all_nodes_are_candidates = true;
        bool has_fluid_transport = false;
        std::vector<ChunkKey> member_chunks;
        std::vector<uint64_t> island_machine_guids;
        std::string dimension_id;
        for (const size_t index : component) {
            const IndustrialGraphNode& node = nodes[index];
            if (!candidates.contains(node.chunk_key)) all_nodes_are_candidates = false;
            member_chunks.push_back(node.chunk_key);
            if (dimension_id.empty()) dimension_id = node.position.dimension_id;
            if (node.kind == IndustrialGraphNodeKind::kPipe &&
                node.pipe_type != PipeType::ITEM) {
                has_fluid_transport = true;
            }
            if (node.kind != IndustrialGraphNodeKind::kMachine) continue;
            has_candidate_machine = has_candidate_machine || candidates.contains(node.chunk_key);
            island_machine_guids.push_back(node.machine_guid);
        }
        if (!has_candidate_machine || !all_nodes_are_candidates) continue;
        if (has_fluid_transport) {
            ++fluid_deferred_components;
            continue;
        }

        std::sort(member_chunks.begin(), member_chunks.end(), chunk_key_less);
        member_chunks.erase(std::unique(member_chunks.begin(), member_chunks.end(), same_chunk),
                            member_chunks.end());
        std::sort(island_machine_guids.begin(), island_machine_guids.end());
        if (member_chunks.empty() || island_machine_guids.empty() ||
            island_machine_guids.front() == 0 || dimension_id.empty()) {
            return invalid_state("Offline industrial topology component has invalid membership");
        }
        if (std::adjacent_find(island_machine_guids.begin(), island_machine_guids.end()) !=
            island_machine_guids.end()) {
            return invalid_state("Offline industrial topology component has duplicate machines");
        }

        OfflineNetworkIslandSnapshot snapshot{
            .island_id = island_machine_guids.front(),
            .dimension_id = dimension_id,
            .anchor_chunk = member_chunks.front(),
            .member_chunks = std::move(member_chunks),
            .topology_revision = topology_revision_for(component, nodes),
            .last_simulated_tick = source_tick,
            .machine_guids = std::move(island_machine_guids),
        };
        std::set<uint64_t> segment_ids;

        const auto append_segments = [&](DisjointSets& resource_graph,
                                         OfflineNetworkResourceKind kind)
            -> snt::core::Expected<void> {
            std::map<size_t, std::vector<size_t>> resource_components;
            for (const size_t index : component) {
                resource_components[resource_graph.find(index)].push_back(index);
            }
            for (const auto& [resource_root, resource_component] : resource_components) {
                static_cast<void>(resource_root);
                bool has_transport = false;
                int64_t maximum_transfer = std::numeric_limits<int64_t>::max();
                int64_t power_capacity = 0;
                std::vector<uint64_t> segment_machine_guids;
                for (const size_t index : resource_component) {
                    const IndustrialGraphNode& node = nodes[index];
                    if (node.kind == IndustrialGraphNodeKind::kMachine) {
                        segment_machine_guids.push_back(node.machine_guid);
                        if (kind == OfflineNetworkResourceKind::kPower) {
                            power_capacity = saturating_add_nonnegative(
                                power_capacity,
                                std::max<int64_t>(node.machine_energy_capacity, 0));
                        }
                        continue;
                    }
                    if (kind == OfflineNetworkResourceKind::kPower &&
                        node.kind == IndustrialGraphNodeKind::kCable) {
                        has_transport = true;
                        maximum_transfer = std::min(maximum_transfer, get_voltage(node.cable_tier));
                    } else if (kind == OfflineNetworkResourceKind::kItem &&
                               node.kind == IndustrialGraphNodeKind::kPipe &&
                               node.pipe_type == PipeType::ITEM) {
                        has_transport = true;
                        maximum_transfer = std::min(
                            maximum_transfer, kDefaultThroughput(node.pipe_type));
                    }
                }
                if (!has_transport || segment_machine_guids.empty()) continue;
                std::sort(segment_machine_guids.begin(), segment_machine_guids.end());
                if (segment_machine_guids.front() == 0 ||
                    std::adjacent_find(segment_machine_guids.begin(),
                                       segment_machine_guids.end()) !=
                        segment_machine_guids.end()) {
                    return invalid_state("Offline industrial transport segment has invalid machines");
                }
                const uint64_t segment_id = reserve_segment_id(
                    segment_id_for(kind, resource_component, nodes), segment_ids);
                if (segment_id == 0) {
                    return invalid_state("Offline industrial transport segment ids are exhausted");
                }
                const int64_t capacity = kind == OfflineNetworkResourceKind::kPower
                    ? power_capacity
                    : 0;
                snapshot.transport_segments.push_back({
                    .segment_id = segment_id,
                    .kind = kind,
                    .machine_guids = std::move(segment_machine_guids),
                    .capacity = capacity,
                    .max_transfer_per_tick = maximum_transfer,
                });
                if (kind == OfflineNetworkResourceKind::kPower) {
                    snapshot.ledgers.push_back({
                        .segment_id = segment_id,
                        .kind = OfflineNetworkResourceKind::kPower,
                        .resource = offline_network_power_ledger_key(),
                        .capacity = capacity,
                        .max_transfer_per_tick = maximum_transfer,
                    });
                }
            }
            return {};
        };

        if (auto result = append_segments(power_graph, OfflineNetworkResourceKind::kPower);
            !result) {
            return result.error();
        }
        if (auto result = append_segments(item_graph, OfflineNetworkResourceKind::kItem);
            !result) {
            return result.error();
        }
        snapshots.push_back(std::move(snapshot));
    }
    if (fluid_deferred_components != 0) {
        SNT_LOG_INFO("Deferred %zu offline industrial component(s) at tick %llu because liquid/gas simulation is not yet available",
                     fluid_deferred_components,
                     static_cast<unsigned long long>(source_tick));
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const OfflineNetworkIslandSnapshot& left,
                 const OfflineNetworkIslandSnapshot& right) {
                  return left.island_id < right.island_id;
              });
    if (std::adjacent_find(snapshots.begin(), snapshots.end(),
                           [](const OfflineNetworkIslandSnapshot& left,
                              const OfflineNetworkIslandSnapshot& right) {
                               return left.island_id == right.island_id;
                           }) != snapshots.end()) {
        return invalid_state("Offline industrial topology has duplicate island identities");
    }
    return snapshots;
}

snt::core::Expected<uint64_t> OfflineIndustrialNetworkIslandSimulator::advance_offline_island(
    OfflineNetworkIslandSnapshot& snapshot,
    GameContentRegistry& content,
    GameChunkSidecarRegistry& sidecars,
    IMachineTickEventSink* event_sink,
    uint64_t first_tick,
    uint64_t tick_count) {
    auto records = find_industrial_records(snapshot, content, sidecars);
    if (!records) return records.error();

    struct PowerSegmentRuntime {
        const OfflineNetworkTransportSegment* segment = nullptr;
        OfflineNetworkResourceLedger* ledger = nullptr;
        std::vector<IndustrialMachineRecord*> machines;
    };
    struct ItemSegmentRuntime {
        const OfflineNetworkTransportSegment* segment = nullptr;
        std::vector<IndustrialMachineRecord*> machines;
    };
    std::vector<PowerSegmentRuntime> power_segments;
    std::vector<ItemSegmentRuntime> item_segments;
    for (const OfflineNetworkTransportSegment& segment : snapshot.transport_segments) {
        auto segment_records = records_for_segment(segment, *records);
        if (!segment_records) return segment_records.error();
        if (segment.kind == OfflineNetworkResourceKind::kPower) {
            const auto ledger = std::find_if(
                snapshot.ledgers.begin(), snapshot.ledgers.end(),
                [&segment](const OfflineNetworkResourceLedger& candidate) {
                    return candidate.segment_id == segment.segment_id &&
                           candidate.kind == OfflineNetworkResourceKind::kPower &&
                           candidate.resource == offline_network_power_ledger_key();
                });
            if (ledger == snapshot.ledgers.end()) {
                return invalid_state("Offline industrial power segment is missing its ledger");
            }
            power_segments.push_back({&segment, &*ledger, std::move(*segment_records)});
        } else if (segment.kind == OfflineNetworkResourceKind::kItem) {
            item_segments.push_back({&segment, std::move(*segment_records)});
        } else {
            return invalid_state(
                "Offline industrial island contains a fluid segment without a fluid simulator");
        }
    }

    for (uint64_t offset = 0; offset < tick_count; ++offset) {
        for (PowerSegmentRuntime& segment : power_segments) {
            if (auto result = transfer_power_segment(
                    *segment.segment, *segment.ledger, segment.machines);
                !result) {
                return result.error();
            }
        }
        for (const ItemSegmentRuntime& segment : item_segments) {
            if (auto result = transfer_item_segment(*segment.segment, segment.machines); !result) {
                return result.error();
            }
        }
        for (IndustrialMachineRecord& machine : *records) {
            auto input = make_machine_execution_input(
                content, snt::ecs::EntityGuid{machine.record->entity_guid},
                GameMachineRuntimePersistence::make_runtime_component(*machine.record));
            if (!input) return input.error();
            input->allow_new_jobs = machine.definition->offline_simulation.can_start_new_jobs &&
                                    !machine.definition->requires_manual_activation;
            MachineExecutionResult result = advance_machine_execution(
                std::move(*input), first_tick + offset, 1);
            GameMachineRuntimePersistence::copy_runtime_to_record(*machine.record, result.machine);
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
