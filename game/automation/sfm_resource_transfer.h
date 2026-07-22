// SFM resource-transfer primitive.
//
// The future SFM graph and UI schedule this service, but the transfer itself
// is deliberately independent of node/editor state. It moves one compact
// ResourceStack between two snapshot-bound IResourceStorage endpoints, so
// items, fluids, gases, and later resource kinds never fork into separate
// ItemId/FluidId execution paths.

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"

namespace snt::game {

struct SfmResourceTransferResult {
    ResourceStack requested;
    ResourceStack transferable;
    ResourceStack transferred;
};

class SfmResourceTransfer final {
public:
    // `kSimulate` performs no mutation and reports the maximum transferable
    // stack. `kExecute` commits that stack in the caller's fixed-tick critical
    // section. Endpoint implementations should honor their simulation result
    // when no intervening mutation occurs. An unexpected destination shortfall
    // is compensated back to the source and reported by `transferred`; failed
    // compensation is returned as an error because resource ownership is then
    // no longer provably conserved.
    [[nodiscard]] static snt::core::Expected<SfmResourceTransferResult> transfer(
        IResourceStorage& source,
        IResourceStorage& destination,
        const ResourceKeyContext& context,
        const ResourceStack& requested,
        ResourceTransferMode mode);
};

}  // namespace snt::game
