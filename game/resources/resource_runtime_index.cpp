#include "game/resources/resource_runtime_index.h"

#include "core/error.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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

}  // namespace

struct ResourceRuntimeIndex::Data {
    struct TypeData {
        std::string semantic_type;
        std::unordered_map<std::string, snt::core::RuntimeKeyId> resource_ids;
        std::vector<std::string> resources_by_id{std::string{}};
        std::unordered_map<std::string, snt::core::RuntimeKeyId> variant_ids;
        std::vector<std::string> variants_by_id{std::string{}};
    };

    std::unordered_map<ResourceKey, RuntimeResourceKey, ResourceKey::Hash> runtime_by_key;
    std::vector<TypeData> types_by_id{TypeData{}};
    uint64_t generation = 0;
};

ResourceRuntimeIndex::ResourceRuntimeIndex() : data_(std::make_shared<Data>()) {}

std::optional<RuntimeResourceKey> ResourceRuntimeIndex::Snapshot::resolve_runtime(
    const ResourceKey& key) const {
    if (!data_) return std::nullopt;
    const auto found = data_->runtime_by_key.find(key);
    if (found == data_->runtime_by_key.end()) return std::nullopt;
    return found->second;
}

std::optional<ResourceKey> ResourceRuntimeIndex::Snapshot::resolve_semantic(
    const RuntimeResourceKey& key) const {
    if (!data_ || !key.is_valid() || key.type_id >= data_->types_by_id.size()) {
        return std::nullopt;
    }
    const Data::TypeData& type = data_->types_by_id[key.type_id];
    if (key.resource_id >= type.resources_by_id.size() ||
        key.variant_id >= type.variants_by_id.size()) {
        return std::nullopt;
    }
    ResourceKey semantic{
        .type = type.semantic_type,
        .id = type.resources_by_id[key.resource_id],
        .variant = type.variants_by_id[key.variant_id],
    };
    // A type-local resource and variant can both exist without their exact
    // combination being registered, so verify the complete identity.
    if (!data_->runtime_by_key.contains(semantic)) return std::nullopt;
    return semantic;
}

uint64_t ResourceRuntimeIndex::Snapshot::generation() const noexcept {
    return data_ ? data_->generation : 0;
}

size_t ResourceRuntimeIndex::Snapshot::size() const noexcept {
    return data_ ? data_->runtime_by_key.size() : 0;
}

snt::core::Expected<void> ResourceRuntimeIndex::rebuild(
    std::span<const ResourceKey> keys) {
    if (data_ && data_->generation == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Resource runtime key index generation is exhausted");
    }

    std::map<std::string, std::set<std::string>, std::less<>> resource_ids_by_type;
    std::map<std::string, std::set<std::string>, std::less<>> variants_by_type;
    std::set<ResourceKey, bool (*)(const ResourceKey&, const ResourceKey&)> unique_keys(
        [](const ResourceKey& left, const ResourceKey& right) {
            if (left.type != right.type) return left.type < right.type;
            if (left.id != right.id) return left.id < right.id;
            return left.variant < right.variant;
        });

    for (const ResourceKey& key : keys) {
        if (!key.is_valid()) {
            return invalid_argument("Resource runtime key index received an invalid key");
        }
        if (!unique_keys.insert(key).second) {
            return invalid_argument("Resource runtime key index keys must be unique");
        }
        resource_ids_by_type[key.type].insert(key.id);
        if (!key.variant.empty()) variants_by_type[key.type].insert(key.variant);
    }
    if (resource_ids_by_type.size() >
        static_cast<size_t>(std::numeric_limits<ResourceRuntimeTypeId>::max())) {
        return invalid_argument("Resource runtime key index contains too many resource types");
    }

    auto candidate = std::make_shared<Data>();
    candidate->types_by_id.reserve(resource_ids_by_type.size() + 1);
    for (const auto& [type_name, resources] : resource_ids_by_type) {
        if (resources.size() >
            static_cast<size_t>(std::numeric_limits<snt::core::RuntimeKeyId>::max())) {
            return invalid_argument("Resource runtime key index contains too many resources");
        }
        const auto type_id = static_cast<ResourceRuntimeTypeId>(
            candidate->types_by_id.size());
        static_cast<void>(type_id);
        Data::TypeData type;
        type.semantic_type = type_name;
        type.resources_by_id.reserve(resources.size() + 1);
        for (const std::string& resource_id : resources) {
            const auto runtime_id = static_cast<snt::core::RuntimeKeyId>(
                type.resources_by_id.size());
            type.resource_ids.emplace(resource_id, runtime_id);
            type.resources_by_id.push_back(resource_id);
        }

        const auto variants = variants_by_type.find(type_name);
        if (variants != variants_by_type.end()) {
            if (variants->second.size() >
                static_cast<size_t>(std::numeric_limits<snt::core::RuntimeKeyId>::max())) {
                return invalid_argument("Resource runtime key index contains too many variants");
            }
            type.variants_by_id.reserve(variants->second.size() + 1);
            for (const std::string& variant : variants->second) {
                const auto runtime_id = static_cast<snt::core::RuntimeKeyId>(
                    type.variants_by_id.size());
                type.variant_ids.emplace(variant, runtime_id);
                type.variants_by_id.push_back(variant);
            }
        }
        candidate->types_by_id.push_back(std::move(type));
    }

    candidate->runtime_by_key.reserve(keys.size());
    for (const ResourceKey& key : keys) {
        const auto type_found = std::find_if(
            candidate->types_by_id.begin() + 1, candidate->types_by_id.end(),
            [&key](const Data::TypeData& type) { return type.semantic_type == key.type; });
        if (type_found == candidate->types_by_id.end()) {
            return invalid_state("Resource runtime key index lost a registered type");
        }
        const auto resource_found = type_found->resource_ids.find(key.id);
        if (resource_found == type_found->resource_ids.end()) {
            return invalid_state("Resource runtime key index lost a registered resource");
        }
        const auto type_id = static_cast<ResourceRuntimeTypeId>(
            std::distance(candidate->types_by_id.begin(), type_found));
        snt::core::RuntimeKeyId variant_id = snt::core::kInvalidRuntimeKeyId;
        if (!key.variant.empty()) {
            const auto variant_found = type_found->variant_ids.find(key.variant);
            if (variant_found == type_found->variant_ids.end()) {
                return invalid_state("Resource runtime key index lost a registered variant");
            }
            variant_id = variant_found->second;
        }
        candidate->runtime_by_key.emplace(key, RuntimeResourceKey{
            .type_id = type_id,
            .resource_id = resource_found->second,
            .variant_id = variant_id,
        });
    }
    candidate->generation = data_ ? data_->generation + 1 : 1;
    data_ = std::move(candidate);
    return {};
}

void ResourceRuntimeIndex::restore(Snapshot snapshot) noexcept {
    if (snapshot.data_) data_ = std::move(snapshot.data_);
}

}  // namespace snt::game
