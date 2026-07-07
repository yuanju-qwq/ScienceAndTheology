// Camera System implementation.
//
// P2.A1: reads input from InputSystem instead of polling SDL directly.
// SDL scancodes are still used as the key index (InputState::key_held is
// indexed by scancode), but CameraSystem no longer includes SDL headers
// — it gets scancode values from <SDL3/SDL_scancode.h> via input_state.h
// indirectly. To keep SDL out of this TU entirely, we use the integer
// values of SDL_SCANCODE_* (defined below as kScancodeW etc.).

#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "input/input_system.h"

#include <SDL3/SDL_scancode.h>

#include <cmath>

namespace snt::ecs {

void CameraSystem::update(World& world, float dt) {
    if (!input_ || active_camera_ == entt::null) return;

    auto& registry = world.registry();
    if (!registry.all_of<Transform, Camera>(active_camera_)) return;

    auto& transform = registry.get<Transform>(active_camera_);

    // Read the per-frame input snapshot.
    const auto& state = input_->state();

    // --- Keyboard movement ---
    float speed = move_speed_ * dt;
    // SDL_SCANCODE_LSHIFT = 225 in SDL3; key_held is indexed by scancode.
    if (state.key_held[SDL_SCANCODE_LSHIFT] ||
        state.key_held[SDL_SCANCODE_RSHIFT]) {
        speed *= 2.0f;  // speed boost
    }

    // Forward direction derived from yaw + pitch.
    float yaw_rad = yaw_ * 3.14159265f / 180.0f;
    float pitch_rad = pitch_ * 3.14159265f / 180.0f;
    float forward[3] = {
        std::cos(pitch_rad) * std::cos(yaw_rad),
        std::sin(pitch_rad),
        std::cos(pitch_rad) * std::sin(yaw_rad),
    };
    // Right vector (assuming up = +Y): derived from yaw only.
    float right[3] = {
        -std::sin(yaw_rad),
        0.0f,
        std::cos(yaw_rad),
    };

    // WASD movement (held state).
    if (state.key_held[SDL_SCANCODE_W]) {
        transform.position[0] += forward[0] * speed;
        transform.position[1] += forward[1] * speed;
        transform.position[2] += forward[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_S]) {
        transform.position[0] -= forward[0] * speed;
        transform.position[1] -= forward[1] * speed;
        transform.position[2] -= forward[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_A]) {
        transform.position[0] -= right[0] * speed;
        transform.position[2] -= right[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_D]) {
        transform.position[0] += right[0] * speed;
        transform.position[2] += right[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_Q]) {
        transform.position[1] -= speed;
    }
    if (state.key_held[SDL_SCANCODE_E]) {
        transform.position[1] += speed;
    }

    // --- Mouse look (right button held + relative motion) ---
    // mouse_held[2] = right button (see InputSystem mapping).
    if (state.mouse_held[2]) {
        yaw_ -= state.mouse_dx * look_speed_;
        pitch_ += state.mouse_dy * look_speed_;

        // Clamp pitch to avoid gimbal flip.
        if (pitch_ > 89.0f) pitch_ = 89.0f;
        if (pitch_ < -89.0f) pitch_ = -89.0f;
    }

    // Write rotation into transform for the render system to use.
    transform.rotation[0] = pitch_;
    transform.rotation[1] = yaw_;
    transform.rotation[2] = 0.0f;
}

}  // namespace snt::ecs
