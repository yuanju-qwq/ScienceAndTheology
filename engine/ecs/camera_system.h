// Camera System — MC-style first-person camera.
//
// P2.A2: rewritten to match Minecraft creative-mode controls.
//   W/A/S/D: move (horizontal plane; A/D strafe, no yaw change)
//   Space:   ascend
//   LShift:  descend
//   Double-tap W: toggle sprint (2x move speed while W held)
//   Mouse:   free-look (relative mouse mode managed by Engine; CameraSystem
//            just reads mouse_dx/dy)
//
// Sprint state machine:
//   - On W press (edge), record timestamp. If previous press was < 400ms
//     ago, set sprint_active_ = true.
//   - While sprint_active_ + W held, multiply speed by 2.
//   - When W is released, sprint_active_ = false (must double-tap again).
//
// Pointer lock is managed by the Engine, not CameraSystem. The Engine
// toggles relative mouse mode based on esc_pressed / wants_mouse_lock.
// When the mouse is unlocked, CameraSystem skips mouse-look so the user
// can interact with the OS cursor.

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

    // Tell CameraSystem whether the mouse is currently locked (relative
    // mode). When false, mouse-look is skipped. Engine calls this each
    // frame after toggling relative mouse mode.
    void set_mouse_locked(bool locked) { mouse_locked_ = locked; }

    void update(World& world, float dt) override;

private:
    snt::input::InputSystem* input_ = nullptr;
    entt::entity active_camera_ = entt::null;

    // Camera state.
    float yaw_ = -90.0f;    // degrees; -90 = looking down -Z
    float pitch_ = 0.0f;    // degrees
    float move_speed_ = 3.0f;   // units per second
    float look_speed_ = 0.1f;   // degrees per pixel

    // Sprint state (double-tap W within 400ms).
    bool  sprint_active_ = false;
    float last_w_press_time_ = -1.0f;  // seconds since engine start; -1 = none
    float time_accumulator_ = 0.0f;    // running time, advanced by dt

    // Mirror of Engine's relative-mouse-mode flag. When false, mouse-look
    // is skipped so the user can move the OS cursor.
    bool mouse_locked_ = false;
};

}  // namespace snt::ecs
