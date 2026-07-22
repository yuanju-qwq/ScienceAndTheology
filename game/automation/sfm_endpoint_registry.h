// SFM endpoint binding and compact transfer scheduling contracts.
//
// A persisted SFM rule refers to world-owned endpoints by a stable address and
// to a resource by ResourceContentStack. Before a fixed-tick schedule runs,
// SfmEndpointRegistry resolves both once into a SfmBoundResourceTransfer. The
// execution path then contains only generation-checked endpoint handles and a
// compact ResourceStack, with no string comparisons or container scans.

#pragma once

#include "core/expected.h"
#include "game/automation/sfm_resource_transfer.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace snt::game {

// Stable endpoint identity supplied by the topology owner, for example a
// canonical world-port or machine-anchor address. Its format is intentionally
// not interpreted by SFM so ships, block entities, and virtual endpoints can
// each retain their own authoritative addressing rules.
struct SfmEndpointAddress {
    std::string value;

    [[nodiscard]] bool is_valid() const noexcept;

    friend bool operator==(const SfmEndpointAddress&,
                           const SfmEndpointAddress&) = default;

    struct Hash {
        [[nodiscard]] size_t operator()(const SfmEndpointAddress& address) const noexcept;
    };
};

// A generation-checked transient endpoint handle. It is valid only while the
// corresponding endpoint remains registered; a removed slot cannot make an
// old compiled rule target a newly registered storage endpoint.
struct SfmEndpointHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const SfmEndpointHandle&,
                           const SfmEndpointHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<SfmEndpointHandle>);

// Durable SFM transfer data. This is suitable for a future program save and
// UI edit boundary; it deliberately cannot execute until compiled against a
// live endpoint registry and resource snapshot.
struct SfmResourceTransferRule {
    SfmEndpointAddress source;
    SfmEndpointAddress destination;
    ResourceContentStack requested;

    [[nodiscard]] bool is_valid() const noexcept {
        return source.is_valid() && destination.is_valid() && requested.is_valid();
    }
};

// Fixed-tick representation of one transfer rule. ResourceKey and
// SfmEndpointHandle are both compact values. The opaque context is retained
// only as a snapshot-identity guard: it prevents an old numeric ResourceKey
// from being executed under a newer mapping after a content reload.
struct SfmBoundResourceTransfer {
    SfmEndpointHandle source;
    SfmEndpointHandle destination;
    ResourceStack requested;
    ResourceKeyContext resource_context;

    [[nodiscard]] bool is_valid() const noexcept {
        return source.is_valid() && destination.is_valid() && requested.is_valid() &&
               resource_context.is_valid();
    }
};

// Main-thread registry of live SFM storage endpoints. Address resolution,
// handle resolution, and compiled transfer dispatch are expected O(1). It
// owns neither endpoint nor storage: the topology owner must unregister an
// endpoint before destroying its IResourceStorage implementation.
class SfmEndpointRegistry final {
public:
    SfmEndpointRegistry();

    SfmEndpointRegistry(const SfmEndpointRegistry&) = delete;
    SfmEndpointRegistry& operator=(const SfmEndpointRegistry&) = delete;

    [[nodiscard]] snt::core::Expected<SfmEndpointHandle> register_endpoint(
        SfmEndpointAddress address,
        IResourceStorage& storage);
    [[nodiscard]] bool unregister_endpoint(SfmEndpointHandle handle) noexcept;

    // Address lookup is used when a saved or edited rule is compiled, never
    // in the fixed-tick transfer path.
    [[nodiscard]] std::optional<SfmEndpointHandle> resolve_endpoint(
        const SfmEndpointAddress& address) const;
    [[nodiscard]] IResourceStorage* find_endpoint(SfmEndpointHandle handle) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return endpoint_slots_.size(); }

    // Resolves one durable rule once against the current resource snapshot.
    // The resulting value must be discarded and rebuilt when its topology or
    // resource runtime snapshot changes.
    [[nodiscard]] snt::core::Expected<SfmBoundResourceTransfer> compile_transfer(
        const SfmResourceTransferRule& rule,
        const IResourceKeyResolver& resource_resolver) const;

    // Dispatches a precompiled transfer without looking at a stable address
    // or resource string. A stale handle is a topology-boundary error and
    // never mutates either endpoint; a mismatched resource snapshot likewise
    // requires recompilation instead of reusing numeric IDs.
    [[nodiscard]] snt::core::Expected<SfmResourceTransferResult> execute_transfer(
        const ResourceKeyContext& context,
        const SfmBoundResourceTransfer& transfer,
        ResourceTransferMode mode) const;

private:
    struct Slot {
        IResourceStorage* storage = nullptr;
        SfmEndpointAddress address;
        uint32_t generation = 1;
    };

    [[nodiscard]] Slot* find_slot(SfmEndpointHandle handle) noexcept;
    [[nodiscard]] const Slot* find_slot(SfmEndpointHandle handle) const noexcept;

    // Slot zero is permanently invalid so a default handle cannot resolve.
    std::vector<Slot> slots_;
    std::vector<uint32_t> reusable_slots_;
    std::unordered_map<SfmEndpointAddress, uint32_t, SfmEndpointAddress::Hash>
        endpoint_slots_;
};

}  // namespace snt::game
