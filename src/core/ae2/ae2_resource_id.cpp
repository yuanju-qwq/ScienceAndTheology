#include "ae2_resource_id.hpp"

namespace science_and_theology::gt {

ResourceId ResourceId::from_key(const ResourceKey& key) {
    const ResourceKeyType* t = &key.get_type();
    if (auto* ik = key.as_item()) {
        return {t, static_cast<uint32_t>(ik->item_id()),
                static_cast<uint64_t>(static_cast<int32_t>(ik->secondary_id()))};
    }
    if (auto* fk = key.as_fluid())     return {t, static_cast<uint32_t>(fk->fluid_id())};
    return {};
}

std::unique_ptr<ResourceKey> ResourceId::to_key() const {
    if (!type) return nullptr;
    const char* id = type->id();
    if (id[0] == 'i' && id[1] == 't') // "item"
        return std::make_unique<ItemKey>(
            static_cast<ItemId>(raw_id),
            static_cast<int32_t>(secondary));
    if (id[0] == 'f' && id[1] == 'l') // "fluid"
        return std::make_unique<FluidKey>(static_cast<FluidId>(raw_id));
    return nullptr;
}

} // namespace science_and_theology::gt
