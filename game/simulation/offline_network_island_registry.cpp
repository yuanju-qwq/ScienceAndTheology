// Durable offline-network island ownership implementation.

#define SNT_LOG_CHANNEL "game.offline_network_island"
#include "game/simulation/offline_network_island_registry.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
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

[[nodiscard]] bool resource_key_less(const ResourceContentKey& left,
                                     const ResourceContentKey& right) noexcept {
    if (left.type != right.type) return left.type < right.type;
    if (left.id != right.id) return left.id < right.id;
    return left.variant < right.variant;
}

[[nodiscard]] bool resource_matches_ledger_kind(
    const OfflineNetworkResourceLedger& ledger) noexcept {
    switch (ledger.kind) {
        case OfflineNetworkResourceKind::kPower:
            return ledger.resource.is_power();
        case OfflineNetworkResourceKind::kItem:
            return ledger.resource.is_item();
        case OfflineNetworkResourceKind::kFluid:
            return ledger.resource.is_fluid();
    }
    return false;
}

[[nodiscard]] bool same_ledger_key(const OfflineNetworkResourceLedger& left,
                                   const OfflineNetworkResourceLedger& right) noexcept {
    return left.segment_id == right.segment_id && left.kind == right.kind &&
           left.resource == right.resource;
}

[[nodiscard]] bool ledger_less(const OfflineNetworkResourceLedger& left,
                               const OfflineNetworkResourceLedger& right) noexcept {
    if (left.segment_id != right.segment_id) return left.segment_id < right.segment_id;
    if (left.kind != right.kind) {
        return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    }
    return resource_key_less(left.resource, right.resource);
}

[[nodiscard]] bool transport_segment_less(const OfflineNetworkTransportSegment& left,
                                          const OfflineNetworkTransportSegment& right) noexcept {
    return left.segment_id < right.segment_id;
}

[[nodiscard]] bool same_transport_segment(const OfflineNetworkTransportSegment& left,
                                          const OfflineNetworkTransportSegment& right) noexcept {
    return left.segment_id == right.segment_id;
}

[[nodiscard]] bool boundary_port_less(const OfflineNetworkBoundaryPort& left,
                                      const OfflineNetworkBoundaryPort& right) noexcept {
    if (left.segment_id != right.segment_id) return left.segment_id < right.segment_id;
    if (left.node_id != right.node_id) return left.node_id < right.node_id;
    if (chunk_key_less(left.adjacent_chunk, right.adjacent_chunk)) return true;
    if (chunk_key_less(right.adjacent_chunk, left.adjacent_chunk)) return false;
    if (left.direction != right.direction) return left.direction < right.direction;
    return left.topology_revision < right.topology_revision;
}

[[nodiscard]] bool same_boundary_port(const OfflineNetworkBoundaryPort& left,
                                      const OfflineNetworkBoundaryPort& right) noexcept {
    return left.segment_id == right.segment_id && left.node_id == right.node_id &&
           same_chunk(left.adjacent_chunk, right.adjacent_chunk) &&
           left.direction == right.direction &&
           left.topology_revision == right.topology_revision;
}

struct MachineRecordLocation {
    ChunkKey chunk_key;
    MachineRuntimePersistenceRecord* record = nullptr;
};

struct ConstMachineRecordLocation {
    ChunkKey chunk_key;
    const MachineRuntimePersistenceRecord* record = nullptr;
};

[[nodiscard]] std::optional<MachineRecordLocation> find_machine_record(
    GameChunkSidecarRegistry& sidecars,
    uint64_t entity_guid) {
    std::optional<MachineRecordLocation> found;
    bool duplicate = false;
    sidecars.for_each([&](const ChunkKey& chunk_key, GameChunkSidecar& sidecar) {
        for (MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (record.entity_guid != entity_guid) continue;
            if (found.has_value()) {
                duplicate = true;
                return;
            }
            found = MachineRecordLocation{chunk_key, &record};
        }
    });
    if (duplicate) return std::nullopt;
    return found;
}

[[nodiscard]] std::optional<ConstMachineRecordLocation> find_machine_record(
    const GameChunkSidecarRegistry& sidecars,
    uint64_t entity_guid) {
    std::optional<ConstMachineRecordLocation> found;
    bool duplicate = false;
    sidecars.for_each([&](const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
        for (const MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (record.entity_guid != entity_guid) continue;
            if (found.has_value()) {
                duplicate = true;
                return;
            }
            found = ConstMachineRecordLocation{chunk_key, &record};
        }
    });
    if (duplicate) return std::nullopt;
    return found;
}

[[nodiscard]] snt::core::Expected<OfflineNetworkIslandSnapshot> normalize_snapshot(
    OfflineNetworkIslandSnapshot snapshot,
    const GameChunkSidecarRegistry& sidecars) {
    if (snapshot.island_id == 0 || snapshot.dimension_id.empty()) {
        return invalid_argument("Offline network island id and dimension must be non-zero/non-empty");
    }
    if (snapshot.dimension_id.find('\0') != std::string::npos ||
        snapshot.anchor_chunk.dimension_id != snapshot.dimension_id) {
        return invalid_argument("Offline network island dimension metadata is invalid");
    }
    if (snapshot.member_chunks.empty() || snapshot.machine_guids.empty()) {
        return invalid_argument("Offline network island requires member chunks and machines");
    }

    std::sort(snapshot.member_chunks.begin(), snapshot.member_chunks.end(), chunk_key_less);
    if (std::adjacent_find(snapshot.member_chunks.begin(), snapshot.member_chunks.end(),
                           same_chunk) != snapshot.member_chunks.end()) {
        return invalid_argument("Offline network island has duplicate member chunks");
    }
    if (!same_chunk(snapshot.member_chunks.front(), snapshot.anchor_chunk)) {
        return invalid_argument("Offline network island anchor must be its first member chunk");
    }
    for (const ChunkKey& member_chunk : snapshot.member_chunks) {
        if (member_chunk.dimension_id != snapshot.dimension_id ||
            sidecars.get(member_chunk) == nullptr) {
            return invalid_state("Offline network island references an unavailable member chunk");
        }
    }

    std::sort(snapshot.machine_guids.begin(), snapshot.machine_guids.end());
    if (snapshot.machine_guids.front() == 0 ||
        std::adjacent_find(snapshot.machine_guids.begin(), snapshot.machine_guids.end()) !=
            snapshot.machine_guids.end()) {
        return invalid_argument("Offline network island machine guids must be unique and non-zero");
    }

    std::sort(snapshot.transport_segments.begin(), snapshot.transport_segments.end(),
              transport_segment_less);
    if (std::adjacent_find(snapshot.transport_segments.begin(),
                           snapshot.transport_segments.end(), same_transport_segment) !=
        snapshot.transport_segments.end()) {
        return invalid_argument("Offline network island has duplicate transport segments");
    }
    for (OfflineNetworkTransportSegment& segment : snapshot.transport_segments) {
        if (segment.segment_id == 0 ||
            static_cast<uint8_t>(segment.kind) >
                static_cast<uint8_t>(OfflineNetworkResourceKind::kFluid) ||
            static_cast<uint8_t>(segment.fluid_transport) >
                static_cast<uint8_t>(OfflineNetworkFluidTransport::kGas) ||
            segment.capacity < 0 || segment.max_transfer_per_tick < 0 ||
            segment.machine_guids.empty()) {
            return invalid_argument("Offline network transport segment is invalid");
        }
        if ((segment.kind == OfflineNetworkResourceKind::kFluid &&
             segment.fluid_transport == OfflineNetworkFluidTransport::kNone) ||
            (segment.kind != OfflineNetworkResourceKind::kFluid &&
             segment.fluid_transport != OfflineNetworkFluidTransport::kNone)) {
            return invalid_argument("Offline network fluid transport class is invalid");
        }
        std::sort(segment.machine_guids.begin(), segment.machine_guids.end());
        if (segment.machine_guids.front() == 0 ||
            std::adjacent_find(segment.machine_guids.begin(),
                               segment.machine_guids.end()) != segment.machine_guids.end()) {
            return invalid_argument(
                "Offline network transport segment machine guids must be unique and non-zero");
        }
        for (const uint64_t entity_guid : segment.machine_guids) {
            if (!std::binary_search(snapshot.machine_guids.begin(),
                                    snapshot.machine_guids.end(), entity_guid)) {
                return invalid_argument(
                    "Offline network transport segment references a non-member machine");
            }
        }
    }

    const auto find_segment = [&snapshot](uint64_t segment_id)
        -> const OfflineNetworkTransportSegment* {
        const auto found = std::lower_bound(
            snapshot.transport_segments.begin(), snapshot.transport_segments.end(), segment_id,
            [](const OfflineNetworkTransportSegment& segment, uint64_t id) {
                return segment.segment_id < id;
            });
        return found != snapshot.transport_segments.end() && found->segment_id == segment_id
            ? &*found
            : nullptr;
    };

    std::sort(snapshot.boundary_ports.begin(), snapshot.boundary_ports.end(), boundary_port_less);
    if (std::adjacent_find(snapshot.boundary_ports.begin(), snapshot.boundary_ports.end(),
                           same_boundary_port) != snapshot.boundary_ports.end()) {
        return invalid_argument("Offline network island has duplicate boundary ports");
    }
    for (const OfflineNetworkBoundaryPort& port : snapshot.boundary_ports) {
        if (port.segment_id == 0 || find_segment(port.segment_id) == nullptr ||
            port.node_id == 0 || port.direction >= 6 ||
            port.adjacent_chunk.dimension_id != snapshot.dimension_id) {
            return invalid_argument("Offline network island boundary port is invalid");
        }
    }

    std::sort(snapshot.ledgers.begin(), snapshot.ledgers.end(), ledger_less);
    if (std::adjacent_find(snapshot.ledgers.begin(), snapshot.ledgers.end(), same_ledger_key) !=
        snapshot.ledgers.end()) {
        return invalid_argument("Offline network island has duplicate resource ledgers");
    }
    for (const OfflineNetworkResourceLedger& ledger : snapshot.ledgers) {
        const OfflineNetworkTransportSegment* const segment = find_segment(ledger.segment_id);
        if (ledger.segment_id == 0 || segment == nullptr || ledger.kind != segment->kind ||
            static_cast<uint8_t>(ledger.kind) >
                static_cast<uint8_t>(OfflineNetworkResourceKind::kFluid) ||
            !ledger.resource.is_valid() || !resource_matches_ledger_kind(ledger) ||
            ledger.capacity < 0 || ledger.stored_amount < 0 ||
            ledger.max_transfer_per_tick < 0 ||
            ledger.stored_amount > ledger.capacity || ledger.capacity != segment->capacity ||
            ledger.max_transfer_per_tick != segment->max_transfer_per_tick) {
            return invalid_argument("Offline network island resource ledger is invalid");
        }
    }
    return snapshot;
}

[[nodiscard]] bool contains_member_chunk(const OfflineNetworkIslandSnapshot& snapshot,
                                         const ChunkKey& chunk_key) {
    return std::binary_search(snapshot.member_chunks.begin(), snapshot.member_chunks.end(),
                              chunk_key, chunk_key_less);
}

[[nodiscard]] snt::core::Expected<void> validate_owned_records(
    const GameChunkSidecarRegistry& sidecars,
    const OfflineNetworkIslandSnapshot& snapshot) {
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        const auto location = find_machine_record(sidecars, entity_guid);
        if (!location.has_value() || location->record == nullptr) {
            return invalid_state("Offline network island references a missing or duplicate machine");
        }
        const MachineRuntimePersistenceRecord& record = *location->record;
        if (!contains_member_chunk(snapshot, location->chunk_key) ||
            record.residency != MachineRuntimeResidency::kOfflineNetworkIsland ||
            record.offline_island_id != snapshot.island_id ||
            record.offline_epoch != snapshot.ownership_epoch) {
            return invalid_state("Offline network island machine ownership disagrees with its snapshot");
        }
    }
    return {};
}

[[nodiscard]] uint64_t increment_epoch(uint64_t value) noexcept {
    return value == std::numeric_limits<uint64_t>::max() ? 0 : value + 1;
}

}  // namespace

OfflineNetworkIslandRegistry::OfflineNetworkIslandRegistry(
    GameChunkSidecarRegistry& sidecars) noexcept
    : sidecars_(&sidecars) {}

snt::core::Expected<void> OfflineNetworkIslandRegistry::initialize() {
    if (sidecars_ == nullptr) return invalid_state("Offline network island registry is unavailable");

    islands_.clear();
    std::set<uint64_t> owned_machine_guids;
    std::optional<snt::core::Error> error;
    sidecars_->for_each([&](const ChunkKey& chunk_key, GameChunkSidecar& sidecar) {
        if (error.has_value()) return;
        for (OfflineNetworkIslandSnapshot& candidate : sidecar.offline_network_islands) {
            auto normalized = normalize_snapshot(candidate, *sidecars_);
            if (!normalized) {
                error = normalized.error();
                return;
            }
            candidate = std::move(*normalized);
            if (!same_chunk(candidate.anchor_chunk, chunk_key) ||
                candidate.ownership_epoch == 0 ||
                !islands_.emplace(candidate.island_id,
                                  IslandLocation{chunk_key, candidate.ownership_epoch}).second) {
                error = invalid_state("Offline network island anchor, epoch, or id is invalid");
                return;
            }
            for (const uint64_t entity_guid : candidate.machine_guids) {
                if (!owned_machine_guids.insert(entity_guid).second) {
                    error = invalid_state("Offline network machine belongs to more than one island");
                    return;
                }
            }
            if (auto ownership = validate_owned_records(*sidecars_, candidate); !ownership) {
                error = ownership.error();
                return;
            }
        }
    });
    if (error.has_value()) {
        islands_.clear();
        return *error;
    }
    if (!islands_.empty()) {
        SNT_LOG_INFO("Recovered %zu offline network island(s) from chunk sidecars", islands_.size());
    }
    return {};
}

snt::core::Expected<OfflineNetworkIslandClaim> OfflineNetworkIslandRegistry::claim(
    OfflineNetworkIslandSnapshot snapshot,
    uint64_t current_tick) {
    if (sidecars_ == nullptr) return invalid_state("Offline network island registry is unavailable");
    auto normalized = normalize_snapshot(std::move(snapshot), *sidecars_);
    if (!normalized) return normalized.error();
    snapshot = std::move(*normalized);
    if (islands_.contains(snapshot.island_id)) {
        return invalid_state("Offline network island id is already owned");
    }

    std::vector<MachineRecordLocation> records;
    records.reserve(snapshot.machine_guids.size());
    uint64_t maximum_epoch = 0;
    for (const uint64_t entity_guid : snapshot.machine_guids) {
        const auto location = find_machine_record(*sidecars_, entity_guid);
        if (!location.has_value() || location->record == nullptr ||
            !contains_member_chunk(snapshot, location->chunk_key) ||
            location->record->residency != MachineRuntimeResidency::kLoaded ||
            location->record->offline_island_id != 0) {
            return invalid_state("Cannot claim an offline network island from a non-loaded machine");
        }
        maximum_epoch = std::max(maximum_epoch, location->record->offline_epoch);
        records.push_back(*location);
    }
    const uint64_t ownership_epoch = increment_epoch(maximum_epoch);
    if (ownership_epoch == 0) {
        return invalid_state("Offline network island ownership epoch is exhausted");
    }
    snapshot.ownership_epoch = ownership_epoch;
    snapshot.last_simulated_tick = current_tick;

    GameChunkSidecar* anchor_sidecar = sidecars_->get(snapshot.anchor_chunk);
    if (anchor_sidecar == nullptr) return invalid_state("Offline network island anchor is unavailable");
    try {
        anchor_sidecar->offline_network_islands.push_back(snapshot);
    } catch (...) {
        return invalid_state("Unable to persist the offline network island snapshot");
    }
    const auto inserted = islands_.emplace(
        snapshot.island_id, IslandLocation{snapshot.anchor_chunk, ownership_epoch});
    if (!inserted.second) {
        anchor_sidecar->offline_network_islands.pop_back();
        return invalid_state("Offline network island id became owned during claim");
    }

    for (const MachineRecordLocation& location : records) {
        location.record->residency = MachineRuntimeResidency::kOfflineNetworkIsland;
        location.record->offline_last_simulated_tick = current_tick;
        location.record->offline_island_id = snapshot.island_id;
        location.record->offline_epoch = ownership_epoch;
    }
    SNT_LOG_INFO("Claimed offline network island %llu (epoch=%llu, chunks=%zu, machines=%zu)",
                 static_cast<unsigned long long>(snapshot.island_id),
                 static_cast<unsigned long long>(ownership_epoch),
                 snapshot.member_chunks.size(), snapshot.machine_guids.size());
    return OfflineNetworkIslandClaim{
        .island_id = snapshot.island_id,
        .ownership_epoch = ownership_epoch,
        .anchor_chunk = snapshot.anchor_chunk,
    };
}

snt::core::Expected<void> OfflineNetworkIslandRegistry::rollback_claim(
    const OfflineNetworkIslandClaim& claim) {
    auto snapshot = prepare_release(claim.island_id, claim.ownership_epoch);
    if (!snapshot) return snapshot.error();
    const auto island = islands_.find(claim.island_id);
    if (island == islands_.end()) return invalid_state("Offline network island is unavailable");

    std::vector<MachineRuntimePersistenceRecord*> records;
    records.reserve(snapshot->machine_guids.size());
    for (const uint64_t entity_guid : snapshot->machine_guids) {
        const auto location = find_machine_record(*sidecars_, entity_guid);
        if (!location.has_value() || location->record == nullptr ||
            increment_epoch(location->record->offline_epoch) == 0) {
            return invalid_state("Cannot roll back exhausted or missing offline network ownership");
        }
        records.push_back(location->record);
    }
    for (MachineRuntimePersistenceRecord* record : records) {
        record->residency = MachineRuntimeResidency::kLoaded;
        record->offline_island_id = 0;
        record->offline_epoch = increment_epoch(record->offline_epoch);
    }

    GameChunkSidecar* anchor_sidecar = sidecars_->get(island->second.anchor_chunk);
    if (anchor_sidecar == nullptr) return invalid_state("Offline network island anchor is unavailable");
    const auto snapshot_it = std::find_if(
        anchor_sidecar->offline_network_islands.begin(),
        anchor_sidecar->offline_network_islands.end(),
        [&claim](const OfflineNetworkIslandSnapshot& candidate) {
            return candidate.island_id == claim.island_id &&
                   candidate.ownership_epoch == claim.ownership_epoch;
        });
    if (snapshot_it == anchor_sidecar->offline_network_islands.end()) {
        return invalid_state("Cannot roll back a missing offline network island snapshot");
    }
    anchor_sidecar->offline_network_islands.erase(snapshot_it);
    islands_.erase(island);
    return {};
}

snt::core::Expected<OfflineNetworkIslandSnapshot>
OfflineNetworkIslandRegistry::prepare_release(uint64_t island_id,
                                              uint64_t ownership_epoch) const {
    const auto found = islands_.find(island_id);
    if (found == islands_.end() || found->second.ownership_epoch != ownership_epoch) {
        return invalid_state("Offline network island ownership is stale or unavailable");
    }
    const OfflineNetworkIslandSnapshot* snapshot = find(island_id);
    if (snapshot == nullptr || snapshot->ownership_epoch != ownership_epoch || sidecars_ == nullptr) {
        return invalid_state("Offline network island snapshot is unavailable");
    }
    if (auto result = validate_owned_records(*sidecars_, *snapshot); !result) {
        return result.error();
    }
    return *snapshot;
}

snt::core::Expected<void> OfflineNetworkIslandRegistry::complete_release(
    uint64_t island_id,
    uint64_t ownership_epoch) {
    const auto found = islands_.find(island_id);
    if (found == islands_.end() || found->second.ownership_epoch != ownership_epoch ||
        sidecars_ == nullptr) {
        return invalid_state("Offline network island ownership is stale or unavailable");
    }
    GameChunkSidecar* anchor_sidecar = sidecars_->get(found->second.anchor_chunk);
    if (anchor_sidecar == nullptr) return invalid_state("Offline network island anchor is unavailable");
    const auto snapshot_it = std::find_if(
        anchor_sidecar->offline_network_islands.begin(),
        anchor_sidecar->offline_network_islands.end(),
        [island_id, ownership_epoch](const OfflineNetworkIslandSnapshot& candidate) {
            return candidate.island_id == island_id &&
                   candidate.ownership_epoch == ownership_epoch;
        });
    if (snapshot_it == anchor_sidecar->offline_network_islands.end()) {
        return invalid_state("Offline network island snapshot is unavailable");
    }
    for (const uint64_t entity_guid : snapshot_it->machine_guids) {
        const auto location = find_machine_record(*sidecars_, entity_guid);
        if (!location.has_value() || location->record == nullptr ||
            location->record->residency != MachineRuntimeResidency::kLoaded ||
            location->record->offline_island_id != 0) {
            return invalid_state("Offline network island release did not restore every machine owner");
        }
    }
    const size_t machine_count = snapshot_it->machine_guids.size();
    anchor_sidecar->offline_network_islands.erase(snapshot_it);
    islands_.erase(found);
    SNT_LOG_INFO("Released offline network island %llu (epoch=%llu, machines=%zu)",
                 static_cast<unsigned long long>(island_id),
                 static_cast<unsigned long long>(ownership_epoch), machine_count);
    return {};
}

OfflineNetworkIslandSnapshot* OfflineNetworkIslandRegistry::find(uint64_t island_id) noexcept {
    const auto found = islands_.find(island_id);
    if (found == islands_.end() || sidecars_ == nullptr) return nullptr;
    GameChunkSidecar* sidecar = sidecars_->get(found->second.anchor_chunk);
    if (sidecar == nullptr) return nullptr;
    const auto snapshot = std::find_if(
        sidecar->offline_network_islands.begin(), sidecar->offline_network_islands.end(),
        [island_id](const OfflineNetworkIslandSnapshot& candidate) {
            return candidate.island_id == island_id;
        });
    return snapshot == sidecar->offline_network_islands.end() ? nullptr : &*snapshot;
}

const OfflineNetworkIslandSnapshot* OfflineNetworkIslandRegistry::find(
    uint64_t island_id) const noexcept {
    const auto found = islands_.find(island_id);
    if (found == islands_.end() || sidecars_ == nullptr) return nullptr;
    const GameChunkSidecar* sidecar = sidecars_->get(found->second.anchor_chunk);
    if (sidecar == nullptr) return nullptr;
    const auto snapshot = std::find_if(
        sidecar->offline_network_islands.begin(), sidecar->offline_network_islands.end(),
        [island_id](const OfflineNetworkIslandSnapshot& candidate) {
            return candidate.island_id == island_id;
        });
    return snapshot == sidecar->offline_network_islands.end() ? nullptr : &*snapshot;
}

std::vector<uint64_t> OfflineNetworkIslandRegistry::island_ids() const {
    std::vector<uint64_t> result;
    result.reserve(islands_.size());
    for (const auto& [island_id, location] : islands_) {
        static_cast<void>(location);
        result.push_back(island_id);
    }
    return result;
}

}  // namespace snt::game
