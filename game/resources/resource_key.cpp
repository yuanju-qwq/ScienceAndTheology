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

ResourceKey ResourceKey::item(std::string id, std::string variant) {
    return {std::string(kResourceTypeItem), std::move(id), std::move(variant)};
}

ResourceKey ResourceKey::fluid(std::string id, std::string variant) {
    return {std::string(kResourceTypeFluid), std::move(id), std::move(variant)};
}

ResourceKey ResourceKey::power(std::string id, std::string variant) {
    return {std::string(kResourceTypePower), std::move(id), std::move(variant)};
}

bool ResourceKey::is_valid() const noexcept {
    return is_valid_component(type) && is_valid_component(id) &&
           variant.find('\0') == std::string::npos;
}

bool ResourceKey::is_item() const noexcept {
    return type == kResourceTypeItem;
}

bool ResourceKey::is_fluid() const noexcept {
    return type == kResourceTypeFluid;
}

bool ResourceKey::is_power() const noexcept {
    return type == kResourceTypePower;
}

ResourceKey ResourceKey::without_variant() const {
    return {type, id, {}};
}

size_t ResourceKey::Hash::operator()(const ResourceKey& key) const noexcept {
    size_t hash = std::hash<std::string>{}(key.type);
    hash = hash_combine(hash, std::hash<std::string>{}(key.id));
    return hash_combine(hash, std::hash<std::string>{}(key.variant));
}

size_t RuntimeResourceKey::Hash::operator()(const RuntimeResourceKey& key) const noexcept {
    size_t hash = static_cast<size_t>(key.type_id);
    hash = hash_combine(hash, static_cast<size_t>(key.resource_id));
    return hash_combine(hash, static_cast<size_t>(key.variant_id));
}

ResourceStack ResourceStack::item(std::string id, int64_t count,
                                  std::string variant) {
    return {ResourceKey::item(std::move(id), std::move(variant)), count};
}

ResourceStack ResourceStack::fluid(std::string id, int64_t millibuckets,
                                   std::string variant) {
    return {ResourceKey::fluid(std::move(id), std::move(variant)), millibuckets};
}

bool ResourceStack::is_valid() const noexcept {
    return amount > 0 && key.is_valid();
}

}  // namespace snt::game
