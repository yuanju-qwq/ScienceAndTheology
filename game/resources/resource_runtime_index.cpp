#include "game/resources/resource_runtime_index.h"

#include "core/error.h"
#include "core/runtime_key_index.h"

#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
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
        snt::core::RuntimeKeyIndex::Snapshot resource_ids;
        snt::core::RuntimeKeyIndex::Snapshot variant_ids;
    };

    std::unordered_map<ResourceContentKey, ResourceKey, ResourceContentKey::Hash>
        runtime_by_key;
    snt::core::RuntimeKeyIndex::Snapshot type_ids;
    std::vector<TypeData> types_by_id{TypeData{}};
    uint64_t generation = 0;
};

ResourceRuntimeIndex::ResourceRuntimeIndex() : data_(std::make_shared<Data>()) {}

ResourceKeyContext ResourceRuntimeIndex::Snapshot::key_context() const noexcept {
    return ResourceRuntimeIndex::make_key_context(data_);
}

std::optional<ResourceKey> ResourceRuntimeIndex::Snapshot::resolve_runtime(
    const ResourceContentKey& key) const {
    if (!data_) return std::nullopt;
    const auto found = data_->runtime_by_key.find(key);
    if (found == data_->runtime_by_key.end()) return std::nullopt;
    return found->second;
}

std::optional<ResourceContentKey> ResourceRuntimeIndex::Snapshot::resolve_content(
    const ResourceKey& key) const {
    if (!data_ || !key.is_valid() || key.kind >= data_->types_by_id.size()) {
        return std::nullopt;
    }
    const auto type_name = data_->type_ids.find_key(key.kind);
    if (!type_name) return std::nullopt;
    const Data::TypeData& type = data_->types_by_id[key.kind];
    const auto resource_id = type.resource_ids.find_key(key.runtime_id);
    if (!resource_id) return std::nullopt;

    std::string variant;
    if (key.variant != snt::core::kInvalidRuntimeKeyId) {
        const auto variant_id = type.variant_ids.find_key(key.variant);
        if (!variant_id) return std::nullopt;
        variant.assign(*variant_id);
    }
    ResourceContentKey content{
        .type = std::string(*type_name),
        .id = std::string(*resource_id),
        .variant = std::move(variant),
    };
    // A type-local resource and variant can both exist without their exact
    // combination being registered, so verify the complete identity.
    if (!data_->runtime_by_key.contains(content)) return std::nullopt;
    return content;
}

uint64_t ResourceRuntimeIndex::Snapshot::generation() const noexcept {
    return data_ ? data_->generation : 0;
}

size_t ResourceRuntimeIndex::Snapshot::size() const noexcept {
    return data_ ? data_->runtime_by_key.size() : 0;
}

snt::core::Expected<void> ResourceRuntimeIndex::rebuild(
    std::span<const ResourceContentKey> keys) {
    if (data_ && data_->generation == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Resource runtime key index generation is exhausted");
    }

    std::map<std::string, std::set<std::string>, std::less<>> resource_ids_by_type;
    std::map<std::string, std::set<std::string>, std::less<>> variants_by_type;
    std::set<ResourceContentKey,
             bool (*)(const ResourceContentKey&, const ResourceContentKey&)> unique_keys(
        [](const ResourceContentKey& left, const ResourceContentKey& right) {
            if (left.type != right.type) return left.type < right.type;
            if (left.id != right.id) return left.id < right.id;
            return left.variant < right.variant;
        });

    for (const ResourceContentKey& key : keys) {
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
        static_cast<size_t>(std::numeric_limits<ResourceKind>::max())) {
        return invalid_argument("Resource runtime key index contains too many resource types");
    }

    std::vector<std::string_view> type_names;
    type_names.reserve(resource_ids_by_type.size());
    for (const auto& [type_name, resources] : resource_ids_by_type) {
        static_cast<void>(resources);
        type_names.push_back(type_name);
    }

    snt::core::RuntimeKeyIndex type_index;
    if (auto result = type_index.rebuild(type_names); !result) return result.error();

    auto candidate = std::make_shared<Data>();
    candidate->type_ids = type_index.snapshot();
    candidate->types_by_id.resize(resource_ids_by_type.size() + 1);
    for (const auto& [type_name, resources] : resource_ids_by_type) {
        const auto type_id = candidate->type_ids.find_id(type_name);
        if (!type_id || *type_id > std::numeric_limits<ResourceKind>::max()) {
            return invalid_state("Resource runtime key index lost a registered type");
        }
        Data::TypeData& type = candidate->types_by_id[*type_id];

        std::vector<std::string_view> resource_names;
        resource_names.reserve(resources.size());
        for (const std::string& resource_id : resources) resource_names.push_back(resource_id);
        snt::core::RuntimeKeyIndex resource_index;
        if (auto result = resource_index.rebuild(resource_names); !result) return result.error();
        type.resource_ids = resource_index.snapshot();

        const auto variants = variants_by_type.find(type_name);
        if (variants != variants_by_type.end()) {
            std::vector<std::string_view> variant_names;
            variant_names.reserve(variants->second.size());
            for (const std::string& variant : variants->second) variant_names.push_back(variant);
            snt::core::RuntimeKeyIndex variant_index;
            if (auto result = variant_index.rebuild(variant_names); !result) return result.error();
            type.variant_ids = variant_index.snapshot();
        }
    }

    candidate->runtime_by_key.reserve(keys.size());
    for (const ResourceContentKey& key : keys) {
        const auto type_id = candidate->type_ids.find_id(key.type);
        if (!type_id || *type_id > std::numeric_limits<ResourceKind>::max()) {
            return invalid_state("Resource runtime key index lost a registered type");
        }
        const Data::TypeData& type = candidate->types_by_id[*type_id];
        const auto resource_id = type.resource_ids.find_id(key.id);
        if (!resource_id) {
            return invalid_state("Resource runtime key index lost a registered resource");
        }
        snt::core::RuntimeKeyId variant_id = snt::core::kInvalidRuntimeKeyId;
        if (!key.variant.empty()) {
            const auto found_variant_id = type.variant_ids.find_id(key.variant);
            if (!found_variant_id) {
                return invalid_state("Resource runtime key index lost a registered variant");
            }
            variant_id = *found_variant_id;
        }
        candidate->runtime_by_key.emplace(key, ResourceKey{
            .kind = static_cast<ResourceKind>(*type_id),
            .runtime_id = *resource_id,
            .variant = variant_id,
        });
    }
    candidate->generation = data_ ? data_->generation + 1 : 1;
    data_ = std::move(candidate);
    return {};
}

void ResourceRuntimeIndex::restore(Snapshot snapshot) noexcept {
    if (snapshot.data_) data_ = std::move(snapshot.data_);
}

ResourceKeyContext ResourceRuntimeIndex::make_key_context(
    std::shared_ptr<const Data> data) noexcept {
    if (!data) return {};
    const uint64_t generation = data->generation;
    return ResourceKeyContext{std::move(data), generation};
}

}  // namespace snt::game
