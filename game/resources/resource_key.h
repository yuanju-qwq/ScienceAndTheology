// Game resource identity and generic storage contracts.
//
// Stable content strings are resolved at content, persistence, replication,
// and UI boundaries. Runtime gameplay uses the fixed-width ResourceKey and
// ResourceStack values below. Physical state such as temperature, pressure,
// flow, durability, and ownership belongs to a FluidState, ItemInstance, or
// terrain-fluid state owned by its respective subsystem, never a key variant.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/runtime_key_index.h"

namespace snt::game {

class ResourceRuntimeIndex;

inline constexpr std::string_view kResourceTypeItem = "item";
inline constexpr std::string_view kResourceTypeFluid = "fluid";
// Power is a semantic network resource rather than an inventory item. It
// gives durable energy ledgers the same type/id/variant identity contract as
// item and fluid ledgers without persisting a runtime numeric ID.
inline constexpr std::string_view kResourceTypePower = "power";

// A persistent/content-facing resource identity. `type` is deliberately a
// stable string instead of a closed enum so content categories can be added
// without changing save or protocol framing. This type never enters a worker
// hot path or a concrete storage implementation.
struct ResourceContentKey {
    std::string type;
    std::string id;
    // Stack identity data, for example a material grade. Mutable per-instance
    // state must use a unique variant or an owning ItemInstance instead.
    std::string variant;

    [[nodiscard]] static ResourceContentKey item(std::string id,
                                                 std::string variant = {});
    [[nodiscard]] static ResourceContentKey fluid(std::string id,
                                                  std::string variant = {});
    [[nodiscard]] static ResourceContentKey power(std::string id,
                                                  std::string variant = {});

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_item() const noexcept;
    [[nodiscard]] bool is_fluid() const noexcept;
    [[nodiscard]] bool is_power() const noexcept;
    [[nodiscard]] ResourceContentKey without_variant() const;

    friend bool operator==(const ResourceContentKey&,
                           const ResourceContentKey&) = default;

    struct Hash {
        [[nodiscard]] size_t operator()(const ResourceContentKey& key) const noexcept;
    };
};

// Serializable representation of a resource quantity. It is deliberately
// limited to content/persistence/replication/UI boundaries; a live inventory,
// machine slot, tank, or logistics endpoint uses ResourceStack instead.
struct ResourceContentStack {
    ResourceContentKey key;
    int64_t amount = 0;

    [[nodiscard]] static ResourceContentStack item(std::string id, int64_t count,
                                                   std::string variant = {});
    [[nodiscard]] static ResourceContentStack fluid(std::string id,
                                                    int64_t millibuckets,
                                                    std::string variant = {});
    [[nodiscard]] static ResourceContentStack power(std::string id, int64_t amount,
                                                    std::string variant = {});

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
    [[nodiscard]] bool is_absent() const noexcept {
        return key.type.empty() && key.id.empty() && key.variant.empty() && amount == 0;
    }
    [[nodiscard]] bool is_item() const noexcept { return key.is_item(); }
    [[nodiscard]] bool is_fluid() const noexcept { return key.is_fluid(); }
    [[nodiscard]] bool has_same_key(const ResourceContentStack& other) const noexcept {
        return key == other.key;
    }

    friend bool operator==(const ResourceContentStack&,
                           const ResourceContentStack&) = default;
};

// Fixed-width resource identity captured by a content/runtime snapshot. This
// is the one ResourceKey used by gameplay, storage, logistics, AE, and worker
// systems. `variant == 0` means that the resolved content key has no variant.
using ResourceKind = uint16_t;
inline constexpr ResourceKind kInvalidResourceKind = 0;

struct ResourceKey {
    ResourceKind kind = kInvalidResourceKind;
    snt::core::RuntimeKeyId runtime_id = snt::core::kInvalidRuntimeKeyId;
    snt::core::RuntimeKeyId variant = snt::core::kInvalidRuntimeKeyId;

    [[nodiscard]] bool is_valid() const noexcept {
        return kind != kInvalidResourceKind &&
               runtime_id != snt::core::kInvalidRuntimeKeyId;
    }
    [[nodiscard]] ResourceKey without_variant() const noexcept {
        return {.kind = kind, .runtime_id = runtime_id};
    }

    friend bool operator==(const ResourceKey&, const ResourceKey&) = default;

    struct Hash {
        [[nodiscard]] size_t operator()(const ResourceKey& key) const noexcept;
    };
};
static_assert(std::is_trivially_copyable_v<ResourceKey>);

// The unique hot-path representation of a resource quantity. Items use whole
// counts and fluids use mB; both use int64_t so automation never forks into a
// second amount type for large volumes.
struct ResourceStack {
    ResourceKey key;
    int64_t amount = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return key.is_valid() && amount > 0;
    }
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
    [[nodiscard]] bool is_absent() const noexcept {
        return key.kind == kInvalidResourceKind &&
               key.runtime_id == snt::core::kInvalidRuntimeKeyId &&
               key.variant == snt::core::kInvalidRuntimeKeyId && amount == 0;
    }

    friend bool operator==(const ResourceStack&, const ResourceStack&) = default;
};
static_assert(std::is_trivially_copyable_v<ResourceStack>);

// Opaque identity for one immutable ResourceRuntimeIndex snapshot. Storage is
// bound to one context so compact IDs from another reload cannot be mixed into
// its slots. The shared ownership also keeps the originating snapshot alive
// while a storage endpoint remains attached to it.
class ResourceKeyContext final {
public:
    ResourceKeyContext() = default;

    [[nodiscard]] bool is_valid() const noexcept { return identity_ != nullptr; }
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool matches(const ResourceKeyContext& other) const noexcept {
        return identity_ == other.identity_;
    }

    friend bool operator==(const ResourceKeyContext& left,
                           const ResourceKeyContext& right) noexcept {
        return left.matches(right);
    }

private:
    friend class ResourceRuntimeIndex;

    ResourceKeyContext(std::shared_ptr<const void> identity,
                       uint64_t generation) noexcept
        : identity_(std::move(identity)), generation_(generation) {}

    std::shared_ptr<const void> identity_;
    uint64_t generation_ = 0;
};

// Resolution happens only at content, persistence, replication, and UI
// boundaries. Implementations publish immutable snapshots so a worker holds
// one coherent mapping for its whole task.
class IResourceKeyResolver {
public:
    virtual ~IResourceKeyResolver() = default;

    [[nodiscard]] virtual ResourceKeyContext key_context() const noexcept = 0;
    [[nodiscard]] virtual std::optional<ResourceKey> resolve_runtime(
        const ResourceContentKey& key) const = 0;
    [[nodiscard]] virtual std::optional<ResourceContentKey> resolve_content(
        const ResourceKey& key) const = 0;
};

// The only generic conversion points between durable content values and
// runtime values. Callers attach policy/logging for an unresolved definition;
// these helpers stay pure because they also run during deterministic reload
// rebinding. An absent stack round-trips as an absent stack, while malformed
// or unresolved non-empty values return nullopt.
[[nodiscard]] std::optional<ResourceStack> resolve_resource_stack(
    const ResourceContentStack& content_stack,
    const IResourceKeyResolver& resolver);
[[nodiscard]] std::optional<ResourceContentStack> resolve_content_stack(
    const ResourceStack& runtime_stack,
    const IResourceKeyResolver& resolver);
[[nodiscard]] std::optional<ResourceStack> rebind_resource_stack(
    const ResourceStack& runtime_stack,
    const IResourceKeyResolver& previous_resolver,
    const IResourceKeyResolver& next_resolver);

enum class ResourceTransferMode : uint8_t {
    kSimulate = 0,
    kExecute = 1,
};

// Hot-path contract for inventories, tanks, logistics endpoints, and digital
// storage. An implementation is attached to exactly one ResourceKeyContext.
// Every operation receives that context explicitly: on a mismatch it must
// perform no mutation and return zero (or an empty list). Implementations keep
// their own slot/capacity/physical rules; this generic API only exchanges
// fixed-width ResourceKey and ResourceStack values.
class IResourceStorage {
public:
    virtual ~IResourceStorage() = default;

    [[nodiscard]] virtual ResourceKeyContext key_context() const noexcept = 0;
    [[nodiscard]] virtual int64_t amount_of(const ResourceKeyContext& context,
                                            const ResourceKey& key) const = 0;
    [[nodiscard]] virtual int64_t insert(const ResourceKeyContext& context,
                                         const ResourceStack& stack,
                                         ResourceTransferMode mode) = 0;
    [[nodiscard]] virtual int64_t extract(const ResourceKeyContext& context,
                                          const ResourceKey& key, int64_t amount,
                                          ResourceTransferMode mode) = 0;
    [[nodiscard]] virtual std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const = 0;
};

}  // namespace snt::game
