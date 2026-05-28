#pragma once

#include <cstdint>
#include <string>

#include "ritual_recipe.hpp"

namespace science_and_theology::magic {

class RitualExecutor {
public:
    struct RitualState {
        const RitualRecipe* recipe = nullptr;
        int elapsed_ticks = 0;
        bool active = false;
    };

    // Start a ritual; returns true if started successfully.
    bool start(RitualState& state, const RitualRecipe* recipe);

    // Tick the ritual; returns true when completed.
    bool tick(RitualState& state);

    // Cancel an in-progress ritual.
    void cancel(RitualState& state);

    float progress_percent(const RitualState& state) const;
    bool is_active(const RitualState& state) const;
};

inline bool RitualExecutor::start(RitualState& state, const RitualRecipe* recipe) {
    if (recipe == nullptr) return false;
    state.recipe = recipe;
    state.elapsed_ticks = 0;
    state.active = true;
    return true;
}

inline bool RitualExecutor::tick(RitualState& state) {
    if (!state.active || state.recipe == nullptr) return false;

    state.elapsed_ticks++;
    if (state.elapsed_ticks >= state.recipe->duration_ticks) {
        state.active = false;
        return true;  // ritual completed
    }
    return false;     // still in progress
}

inline void RitualExecutor::cancel(RitualState& state) {
    state.recipe = nullptr;
    state.elapsed_ticks = 0;
    state.active = false;
}

inline float RitualExecutor::progress_percent(const RitualState& state) const {
    if (state.recipe == nullptr || state.recipe->duration_ticks <= 0) return 0.0f;
    return static_cast<float>(state.elapsed_ticks) /
           static_cast<float>(state.recipe->duration_ticks);
}

inline bool RitualExecutor::is_active(const RitualState& state) const {
    return state.active;
}

} // namespace science_and_theology::magic
