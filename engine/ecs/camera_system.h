// Camera System — handles WASD + mouse flight-style camera movement.
//
// Controls:
//   W/S: move forward/backward
//   A/D: move left/right
//   Q/E: move down/up
//   Right-drag: look around (yaw + pitch)
//   Shift: speed boost (2x)
//
// Updates the Transform of the active Camera entity.
// The render system reads this Transform to build the view matrix.

#pragma once

#include "ecs/system.h"

#include <entt/entt.hpp>

namespace snt::platform {
class Window;
}

namespace snt::ecs {

class CameraSystem : public System {
public:
    CameraSystem() = default;
    ~CameraSystem() override = default;

    // Set the window to read input from.
    void set_window(snt::platform::Window* window) { window_ = window; }

    // Set the entity to use as the active camera.
    void set_active_camera(entt::entity e) { active_camera_ = e; }

    void update(World& world, float dt) override;

private:
    snt::platform::Window* window_ = nullptr;
    entt::entity active_camera_ = entt::null;

    // Camera state.
    float yaw_ = -90.0f;    // degrees; -90 = looking down -Z
    float pitch_ = 0.0f;    // degrees
    float move_speed_ = 3.0f;   // units per second
    float look_speed_ = 0.1f;   // degrees per pixel
};

}  // namespace snt::ecs
