#include "i_container_access.hpp"

namespace science_and_theology::sfm {

ContainerId ContainerRegistry::register_container(
        std::unique_ptr<IContainerAccess> access) {
    if (!access) return kInvalidContainerId;
    ContainerId id = next_id_++;
    uint32_t index = static_cast<uint32_t>(containers_.size()) + 1;
    id_to_index_[id] = index;
    index_to_id_[index] = id;
    containers_[id] = std::move(access);
    return id;
}

bool ContainerRegistry::unregister_container(ContainerId id) {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return false;
    uint32_t index = it->second;
    id_to_index_.erase(it);
    index_to_id_.erase(index);
    containers_.erase(id);
    return true;
}

IContainerAccess* ContainerRegistry::get_by_index(uint32_t index) const {
    auto it = index_to_id_.find(index);
    if (it == index_to_id_.end()) return nullptr;
    auto cit = containers_.find(it->second);
    return cit == containers_.end() ? nullptr : cit->second.get();
}

std::vector<std::pair<uint32_t, std::string>>
ContainerRegistry::list_containers() const {
    std::vector<std::pair<uint32_t, std::string>> result;
    for (const auto& entry : index_to_id_) {
        uint32_t idx = entry.first;
        ContainerId cid = entry.second;
        auto it = containers_.find(cid);
        if (it != containers_.end()) {
            result.emplace_back(idx, it->second->get_display_name());
        }
    }
    return result;
}

} // namespace science_and_theology::sfm
