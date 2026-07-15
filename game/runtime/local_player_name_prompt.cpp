// SDL first-run local player-name input window.

#define SNT_LOG_CHANNEL "game.identity_prompt"
#include "game/runtime/local_player_name_prompt.h"

#include "core/error.h"
#include "core/log.h"

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error cancelled(std::string message) {
    return {snt::core::ErrorCode::kCancelled, std::move(message)};
}

[[nodiscard]] snt::core::Error platform_error(std::string message) {
    return {snt::core::ErrorCode::kPlatformInitFailed, std::move(message)};
}

void erase_last_utf8_codepoint(std::string& value) {
    if (value.empty()) return;
    size_t erase_from = value.size() - 1;
    while (erase_from > 0 &&
           (static_cast<unsigned char>(value[erase_from]) & 0xC0u) == 0x80u) {
        --erase_from;
    }
    value.erase(erase_from);
}

void update_window_title(SDL_Window* window, const std::string& display_name) {
    std::string title = "ScienceAndTheology - Local Player Name";
    if (!display_name.empty()) title += ": " + display_name;
    SDL_SetWindowTitle(window, title.c_str());
}

void render_prompt(SDL_Renderer* renderer, const std::string& display_name,
                   const std::string& validation_error) {
    SDL_SetRenderDrawColor(renderer, 24, 31, 38, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 214, 223, 232, 255);
    SDL_RenderDebugText(renderer, 28.0f, 30.0f, "LOCAL PLAYER NAME");
    SDL_RenderDebugText(renderer, 28.0f, 58.0f, "Type a name. Enter confirms. Esc cancels.");
    const std::string name_line = display_name.empty() ? "Name: _" : "Name: " + display_name;
    SDL_RenderDebugText(renderer, 28.0f, 94.0f, name_line.c_str());
    if (!validation_error.empty()) {
        SDL_SetRenderDrawColor(renderer, 240, 120, 100, 255);
        SDL_RenderDebugText(renderer, 28.0f, 132.0f, validation_error.c_str());
    }
    SDL_RenderPresent(renderer);
}

}  // namespace

snt::core::Expected<std::string> SdlLocalPlayerNamePrompt::request_local_player_name() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return platform_error(std::string("SDL_Init failed while opening player-name prompt: ") +
                              SDL_GetError());
    }

    SDL_Window* window = SDL_CreateWindow("ScienceAndTheology - Local Player Name", 560, 190, 0);
    if (window == nullptr) {
        return platform_error(std::string("SDL_CreateWindow failed while opening player-name prompt: ") +
                              SDL_GetError());
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        return platform_error(std::string("SDL_CreateRenderer failed while opening player-name prompt: ") +
                              SDL_GetError());
    }
    if (!SDL_StartTextInput(window)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return platform_error(std::string("SDL_StartTextInput failed for player-name prompt: ") +
                              SDL_GetError());
    }

    const SDL_WindowID window_id = SDL_GetWindowID(window);
    std::string display_name;
    std::string validation_error;
    bool open = true;
    while (open) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == window_id)) {
                SDL_StopTextInput(window);
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                return cancelled("Local player-name selection was cancelled");
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id &&
                event.key.repeat == 0) {
                if (event.key.key == SDLK_ESCAPE) {
                    SDL_StopTextInput(window);
                    SDL_DestroyRenderer(renderer);
                    SDL_DestroyWindow(window);
                    return cancelled("Local player-name selection was cancelled");
                }
                if (event.key.key == SDLK_BACKSPACE) {
                    erase_last_utf8_codepoint(display_name);
                    validation_error.clear();
                    update_window_title(window, display_name);
                    continue;
                }
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_RETURN2) {
                    auto identity = make_local_name_player_identity(display_name);
                    if (identity) {
                        SDL_StopTextInput(window);
                        SDL_DestroyRenderer(renderer);
                        SDL_DestroyWindow(window);
                        return display_name;
                    }
                    validation_error = identity.error().message();
                }
            }
            if (event.type == SDL_EVENT_TEXT_INPUT && event.text.windowID == window_id &&
                event.text.text != nullptr) {
                const std::string candidate = display_name + event.text.text;
                if (candidate.size() <= kMaxPlayerDisplayNameBytes) {
                    display_name = candidate;
                    validation_error.clear();
                    update_window_title(window, display_name);
                }
            }
        }

        render_prompt(renderer, display_name, validation_error);
        SDL_Delay(16);
    }

    SDL_StopTextInput(window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return cancelled("Local player-name selection was cancelled");
}

}  // namespace snt::game
