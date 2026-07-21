#include "game/resources/resource_key.h"

#include <functional>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] bool is_valid_component(std::string_view value) noexcept {
    return !value.empty() && value.find('\0') == std::string_view::npos;
}

[[nodiscard]] size_t hash_combine(size_t seed, size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u));
}

}  // namespace

ResourceContentKey ResourceContentKey::item(std::string id, std::string variant) {
    return {std::string(kResourceTypeItem), std::move(id), std::move(variant)};
}

ResourceContentKey ResourceContentKey::fluid(std::string id, std::string variant) {
    return {std::string(kResourceTypeFluid), std::move(id), std::move(variant)};
}

ResourceContentKey ResourceContentKey::power(std::string id, std::string variant) {
    return {std::string(kResourceTypePower), std::move(id), std::move(variant)};
}

bool ResourceContentKey::is_valid() const noexcept {
    return is_valid_component(type) && is_valid_component(id) &&
           variant.find('\0') == std::string::npos;
}

bool ResourceContentKey::is_item() const noexcept {
    return type == kResourceTypeItem;
}

bool ResourceContentKey::is_fluid() const noexcept {
    return type == kResourceTypeFluid;
}

bool ResourceContentKey::is_power() const noexcept {
    return type == kResourceTypePower;
}

ResourceContentKey ResourceContentKey::without_variant() const {
    return {type, id, {}};
}

size_t ResourceContentKey::Hash::operator()(const ResourceContentKey& key) const noexcept {
    size_t hash = std::hash<std::string>{}(key.type);
    hash = hash_combine(hash, std::hash<std::string>{}(key.id));
    return hash_combine(hash, std::hash<std::string>{}(key.variant));
}

size_t ResourceKey::Hash::operator()(const ResourceKey& key) const noexcept {
    size_t hash = static_cast<size_t>(key.kind);
    hash = hash_combine(hash, static_cast<size_t>(key.runtime_id));
    return hash_combine(hash, static_cast<size_t>(key.variant));
}

ResourceContentStack ResourceContentStack::item(std::string id, int64_t count,
                                                 std::string variant) {
    return {ResourceContentKey::item(std::move(id), std::move(variant)), count};
}

ResourceContentStack ResourceContentStack::fluid(std::string id, int64_t millibuckets,
                                                  std::string variant) {
    return {ResourceContentKey::fluid(std::move(id), std::move(variant)), millibuckets};
}

ResourceContentStack ResourceContentStack::power(std::string id, int64_t amount,
                                                  std::string variant) {
    return {ResourceContentKey::power(std::move(id), std::move(variant)), amount};
}

bool ResourceContentStack::is_valid() const noexcept {
    return amount > 0 && key.is_valid();
}

std::optional<ResourceStack> resolve_resource_stack(
    const ResourceContentStack& content_stack,
    const IResourceKeyResolver& resolver) {
    if (content_stack.is_absent()) return ResourceStack{};
    if (!content_stack.is_valid() || !resolver.key_context().is_valid()) return std::nullopt;
    const auto key = resolver.resolve_runtime(content_stack.key);
    if (!key) return std::nullopt;
    return ResourceStack{.key = *key, .amount = content_stack.amount};
}

std::optional<ResourceContentStack> resolve_content_stack(
    const ResourceStack& runtime_stack,
    const IResourceKeyResolver& resolver) {
    if (runtime_stack.is_absent()) return ResourceContentStack{};
    if (!runtime_stack.is_valid() || !resolver.key_context().is_valid()) return std::nullopt;
    const auto key = resolver.resolve_content(runtime_stack.key);
    if (!key) return std::nullopt;
    return ResourceContentStack{.key = *key, .amount = runtime_stack.amount};
}

std::optional<ResourceStack> rebind_resource_stack(
    const ResourceStack& runtime_stack,
    const IResourceKeyResolver& previous_resolver,
    const IResourceKeyResolver& next_resolver) {
    const ResourceKeyContext previous_context = previous_resolver.key_context();
    const ResourceKeyContext next_context = next_resolver.key_context();
    if (!previous_context.is_valid() || !next_context.is_valid()) return std::nullopt;
    if (previous_context.matches(next_context)) {
        return runtime_stack;
    }
    const auto content_stack = resolve_content_stack(runtime_stack, previous_resolver);
    if (!content_stack) return std::nullopt;
    return resolve_resource_stack(*content_stack, next_resolver);
}

}  // namespace snt::game
