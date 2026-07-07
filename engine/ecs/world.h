// World — owns the ECS registry and manages systems.
//
// Wraps entt::registry, providing:
//   - entity creation + component attachment
//   - system registration + per-frame update
//   - view/group access for systems to query entities
//
// P1.5: basic entity + system management.
// P2+: system scheduling (dependencies, parallel update via JobSystem).

#pragma once

#include <entt/entt.hpp>

#include <memory>
#include <vector>

namespace snt::ecs {

class System;

class World {
public:
    World() = default;
    ~World() = default;

    // Non-copyable; the registry owns entity data.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // --- Entity management ---

    // Create a new entity. Returns its handle.
    entt::entity create_entity() {
        return registry_.create();
    }

    // Destroy an entity and all its components.
    void destroy_entity(entt::entity e) {
        registry_.destroy(e);
    }

    // Attach a component to an entity. Returns a reference to the component.
    template<typename Component, typename... Args>
    Component& add_component(entt::entity e, Args&&... args) {
        return registry_.emplace<Component>(e, std::forward<Args>(args)...);
    }

    // Get a component from an entity.
    template<typename Component>
    Component& get_component(entt::entity e) {
        return registry_.get<Component>(e);
    }

    // --- System management ---

    // Register a system. World takes ownership.
    template<typename SystemType, typename... Args>
    SystemType& add_system(Args&&... args) {
        auto sys = std::make_unique<SystemType>(std::forward<Args>(args)...);
        auto& ref = *sys;
        systems_.push_back(std::move(sys));
        return ref;
    }

    // --- Per-frame update ---
    // Calls update(dt) on all registered systems in registration order.
    void update(float dt) {
        for (auto& sys : systems_) {
            sys->update(*this, dt);
        }
    }

    // --- Registry access (for systems to query entities) ---
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

private:
    entt::registry registry_;
    std::vector<std::unique_ptr<System>> systems_;
};

}  // namespace snt::ecs
