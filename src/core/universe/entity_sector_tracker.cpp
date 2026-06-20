#include "entity_sector_tracker.hpp"

namespace science_and_theology {

void EntitySectorTracker::register_entity(EntityTrackingId entity,
                                          const GlobalPos& pos,
                                          const SectorManager& sector_manager) {
    GlobalBlockPos block_pos = to_block_pos(pos);
    SectorQueryResult q = sector_manager.find_sector(block_pos);

    std::lock_guard<std::mutex> lock(mutex_);
    EntityTrackingState state;
    state.entity = entity;
    state.pos = pos;
    state.current_sector = q.found() ? q.sector : SectorId{0};
    entities_[entity.value] = state;
}

void EntitySectorTracker::unregister_entity(EntityTrackingId entity) {
    std::lock_guard<std::mutex> lock(mutex_);
    entities_.erase(entity.value);
}

std::optional<SectorMigrationEvent> EntitySectorTracker::update_entity_position(
    EntityTrackingId entity,
    const GlobalPos& pos,
    const SectorManager& sector_manager) {

    GlobalBlockPos block_pos = to_block_pos(pos);
    SectorQueryResult q = sector_manager.find_sector(block_pos);

    std::optional<SectorMigrationEvent> result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entities_.find(entity.value);
        if (it == entities_.end()) {
            // 未注册实体，自动注册
            EntityTrackingState state;
            state.entity = entity;
            state.pos = pos;
            state.current_sector = q.found() ? q.sector : SectorId{0};
            entities_[entity.value] = state;
            return result;
        }

        EntityTrackingState& state = it->second;
        state.pos = pos;

        SectorId new_sector = q.found() ? q.sector : SectorId{0};
        if (new_sector != state.current_sector) {
            SectorMigrationEvent event;
            event.entity = entity;
            event.from_sector = state.current_sector;
            event.to_sector = new_sector;
            event.pos = pos;
            result = event;

            state.current_sector = new_sector;
        }
    }

    return result;
}

std::vector<SectorMigrationEvent> EntitySectorTracker::update_positions(
    const std::vector<std::pair<EntityTrackingId, GlobalPos>>& updates,
    const SectorManager& sector_manager) {

    std::vector<SectorMigrationEvent> events;
    for (const auto& [entity, pos] : updates) {
        auto event = update_entity_position(entity, pos, sector_manager);
        if (event.has_value()) {
            events.push_back(*event);
        }
    }
    return events;
}

SectorId EntitySectorTracker::get_entity_sector(EntityTrackingId entity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entity.value);
    if (it == entities_.end()) {
        return SectorId{0};
    }
    return it->second.current_sector;
}

std::optional<GlobalPos> EntitySectorTracker::get_entity_position(
    EntityTrackingId entity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entity.value);
    if (it == entities_.end()) {
        return std::nullopt;
    }
    return it->second.pos;
}

std::vector<EntityTrackingId> EntitySectorTracker::get_entities_in_sector(
    SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EntityTrackingId> result;
    for (const auto& [id, state] : entities_) {
        if (state.current_sector == sector) {
            result.push_back(state.entity);
        }
    }
    return result;
}

size_t EntitySectorTracker::entity_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

void EntitySectorTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entities_.clear();
}

} // namespace science_and_theology
