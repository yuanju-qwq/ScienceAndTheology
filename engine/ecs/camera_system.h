// Camera System — handles WASD + mouse flight-style camera movement.
//
// P2.A1: decoupled from SDL/Window. Reads from InputSystem::state() each
// frame instead of polling SDL directly. This makes CameraSystem a pure
// consumer of input state — it can be unit-tested without a window and
// can be driven by any input source (recorded playback, network, etc.).
//
// Controls:
//   W/S: move forward/backward
//   A/D: move left/right
//   Q/E: move down/up
//   Right-drag: look around (yaw + pitch)
//   Shift: speed boost (2x)

#pragma once

#include "ecs/system.h"
#include "input/input_state.h"

#include <entt/entt.hpp>

namespace snt::input {
class InputSystem;
}

namespace snt::ecs {

class CameraSystem : public System {
public:
    CameraSystem() = default;
    ~CameraSystem() override = default;

    // Set the input source to read from each frame. Required before update().
    void set_input(snt::input::InputSystem* input) { input_ = input; }

    // Set the entity to use as the active camera.
    void set_active_camera(entt::entity e) { active_camera_ = e; }

    void update(World& world, float dt) override;

private:
    snt::input::InputSystem* input_ = nullptr;
    entt::entity active_camera_ = entt::null;

    // Camera state.
    float yaw_ = -90.0f;    // degrees; -90 = looking down -Z
    float pitch_ = 0.0f;    // degrees
    float move_speed_ = 3.0f;   // units per second
    float look_speed_ = 0.1f;   // degrees per pixel
};

}  // namespace snt::ecs
