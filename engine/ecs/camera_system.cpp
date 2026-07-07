// Camera System implementation.

#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "platform/window.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

namespace snt::ecs {

void CameraSystem::update(World& world, float dt) {
    if (!window_ || active_camera_ == entt::null) return;

    auto& registry = world.registry();
    if (!registry.all_of<Transform, Camera>(active_camera_)) return;

    auto& transform = registry.get<Transform>(active_camera_);

    // --- Read keyboard input ---
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const Uint8* keys8 = reinterpret_cast<const Uint8*>(keys);

    float speed = move_speed_ * dt;
    if (keys8[SDL_SCANCODE_LSHIFT] || keys8[SDL_SCANCODE_RSHIFT]) {
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
    // Right = forward x up(0,1,0)
    float right[3] = {
        forward[2] * 0.0f - forward[1] * 0.0f,  // not used, simplified below
        0.0f,
        0.0f,
    };
    // Simplified right vector (assuming up = +Y):
    right[0] = -std::sin(yaw_rad);
    right[2] = std::cos(yaw_rad);

    // WASD movement.
    if (keys8[SDL_SCANCODE_W]) {
        transform.position[0] += forward[0] * speed;
        transform.position[1] += forward[1] * speed;
        transform.position[2] += forward[2] * speed;
    }
    if (keys8[SDL_SCANCODE_S]) {
        transform.position[0] -= forward[0] * speed;
        transform.position[1] -= forward[1] * speed;
        transform.position[2] -= forward[2] * speed;
    }
    if (keys8[SDL_SCANCODE_A]) {
        transform.position[0] -= right[0] * speed;
        transform.position[2] -= right[2] * speed;
    }
    if (keys8[SDL_SCANCODE_D]) {
        transform.position[0] += right[0] * speed;
        transform.position[2] += right[2] * speed;
    }
    if (keys8[SDL_SCANCODE_Q]) {
        transform.position[1] -= speed;
    }
    if (keys8[SDL_SCANCODE_E]) {
        transform.position[1] += speed;
    }

    // --- Mouse look (right button drag) ---
    Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    if (mouse_buttons & SDL_BUTTON_RMASK) {
        float mouse_x, mouse_y;
        SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

        yaw_ += mouse_x * look_speed_;
        pitch_ -= mouse_y * look_speed_;

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
