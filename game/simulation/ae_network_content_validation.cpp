// Current-content validation for durable AE node records.
//
// Structural sidecar validation remains in ae_network_persistence.cpp so it
// can be tested without a script/content runtime. This bridge resolves stable
// node keys against the active catalog only at world-load boundaries.

#define SNT_LOG_CHANNEL "game.ae_network_content_validation"
#include "game/simulation/ae_network_persistence.h"

#include "core/error.h"
#include "game/client/game_content_registry.h"

#include <optional>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

snt::core::Expected<void> GameAeNetworkPersistence::validate_content_references(
    const GameChunkSidecarRegistry& sidecars,
    const GameContentRegistry& content) {
    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const AeNetworkNodePersistenceRecord& record : sidecar.ae_network_node_records) {
            if (record.type == AeNetworkNodeType::kController) continue;
            const AeNetworkNodePlacementDefinition* const placement =
                content.find_ae_network_node_placement_by_node_key(record.node_key);
            if (placement == nullptr || placement->type != record.type) {
                error = invalid_state("Persisted AE node key '" + record.node_key +
                                      "' has no matching current placement definition");
                return;
            }
        }
    });
    if (error) return *error;
    return {};
}

}  // namespace snt::game
