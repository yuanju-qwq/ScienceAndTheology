// Game resource identity and generic storage contracts.
//
// This is the current-only replacement for the legacy item/fluid identifier
// split. A ResourceKey names one immutable, stackable resource identity while
// ResourceStack carries its quantity. Physical state such as temperature,
// pressure, durability, and ownership belongs to the owning simulation or
// item-instance model, not to this generic resource contract.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/runtime_key_index.h"

namespace snt::game {

inline constexpr std::string_view kResourceTypeItem = "item";
inline constexpr std::string_view kResourceTypeFluid = "fluid";

// A persistent semantic resource identity. `type` is deliberately a stable
// string instead of a closed enum so future game-owned resource categories
// can be introduced without changing this value contract or save format.
struct ResourceKey {
    std::string type;
    std::string id;
    // Stack identity data, for example a material grade. Mutable per-instance
    // state must use a unique variant or an owning ItemInstance instead.
    std::string variant;

    [[nodiscard]] static ResourceKey item(std::string id,
                                          std::string variant = {});
    [[nodiscard]] static ResourceKey fluid(std::string id,
                                           std::string variant = {});

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_item() const noexcept;
    [[nodiscard]] bool is_fluid() const noexcept;
    [[nodiscard]] ResourceKey without_variant() const;

    friend bool operator==(const ResourceKey&, const ResourceKey&) = default;

    struct Hash {
        [[nodiscard]] size_t operator()(const ResourceKey& key) const noexcept;
    };
};

// A resource identity plus an amount. Items use whole counts; fluids use mB.
// The amount domain is int64_t so storage and automation never need a
// different stack type merely because a fluid volume is large.
struct ResourceStack {
    ResourceKey key;
    int64_t amount = 0;

    [[nodiscard]] static ResourceStack item(std::string id, int64_t count,
                                            std::string variant = {});
    [[nodiscard]] static ResourceStack fluid(std::string id, int64_t millibuckets,
                                             std::string variant = {});

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
    [[nodiscard]] bool is_item() const noexcept { return key.is_item(); }
    [[nodiscard]] bool is_fluid() const noexcept { return key.is_fluid(); }
    [[nodiscard]] bool has_same_key(const ResourceStack& other) const noexcept {
        return key == other.key;
    }

    friend bool operator==(const ResourceStack&, const ResourceStack&) = default;
};

// Fixed-width identity captured by a content/runtime snapshot. All hot-path
// storage, automation, and simulation code uses this form, never strings.
// `variant_id == 0` means that the semantic key has no stack variant.
using ResourceRuntimeTypeId = uint16_t;
inline constexpr ResourceRuntimeTypeId kInvalidResourceRuntimeTypeId = 0;

struct RuntimeResourceKey {
    ResourceRuntimeTypeId type_id = kInvalidResourceRuntimeTypeId;
    snt::core::RuntimeKeyId resource_id = snt::core::kInvalidRuntimeKeyId;
    snt::core::RuntimeKeyId variant_id = snt::core::kInvalidRuntimeKeyId;

    [[nodiscard]] bool is_valid() const noexcept {
        return type_id != kInvalidResourceRuntimeTypeId &&
               resource_id != snt::core::kInvalidRuntimeKeyId;
    }

    friend bool operator==(const RuntimeResourceKey&,
                           const RuntimeResourceKey&) = default;

    struct Hash {
        [[nodiscard]] size_t operator()(const RuntimeResourceKey& key) const noexcept;
    };
};

struct RuntimeResourceStack {
    RuntimeResourceKey key;
    int64_t amount = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return key.is_valid() && amount > 0;
    }
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
};

// Resolution happens only at content, persistence, replication, and UI
// boundaries. Implementations publish immutable snapshots so a worker holds
// one coherent mapping for its whole task.
class IResourceKeyResolver {
public:
    virtual ~IResourceKeyResolver() = default;

    [[nodiscard]] virtual std::optional<RuntimeResourceKey> resolve_runtime(
        const ResourceKey& key) const = 0;
    [[nodiscard]] virtual std::optional<ResourceKey> resolve_semantic(
        const RuntimeResourceKey& key) const = 0;
};

enum class ResourceTransferMode : uint8_t {
    kSimulate = 0,
    kExecute = 1,
};

// Hot-path contract for inventories, tanks, logistics endpoints, and digital
// storage. Implementations keep their own slot/capacity/physical rules; the
// generic API only exchanges fixed-width runtime identities and amounts.
class IResourceStorage {
public:
    virtual ~IResourceStorage() = default;

    [[nodiscard]] virtual int64_t amount_of(const RuntimeResourceKey& key) const = 0;
    [[nodiscard]] virtual int64_t insert(const RuntimeResourceStack& stack,
                                         ResourceTransferMode mode) = 0;
    [[nodiscard]] virtual int64_t extract(const RuntimeResourceKey& key, int64_t amount,
                                          ResourceTransferMode mode) = 0;
    [[nodiscard]] virtual std::vector<RuntimeResourceKey> stored_keys() const = 0;
};

}  // namespace snt::game
