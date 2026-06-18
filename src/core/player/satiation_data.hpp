#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "magic/rune_def.hpp"

namespace science_and_theology {

// ============================================================
// HungerLevel — severity of hunger
// ============================================================
enum class HungerLevel : uint8_t {
    SATIATED = 0,
    PECKISH,
    HUNGRY,
    STARVING,
    COUNT
};

inline const char* hunger_level_name(HungerLevel level) {
    switch (level) {
        case HungerLevel::SATIATED:  return "satiated";
        case HungerLevel::PECKISH:   return "peckish";
        case HungerLevel::HUNGRY:    return "hungry";
        case HungerLevel::STARVING:  return "starving";
        default:                     return "unknown";
    }
}

// ============================================================
// FoodDef — definition of a food item's nutritional properties
// ============================================================
//
// Each food has a satiation value (hunger restoration) and a list
// of element essence contributions. When a player eats this food,
// both satiation and element essence are restored.
//
// Helper functions are provided to create common food patterns:
// - Plant-based food: large life essence (EARTH/WATER/LIGHT mix)
// - Creature-based food: dominant creature element + small life essence

struct FoodDef {
    float food_value = 0.0f;
    // List of (element, essence_value) pairs.
    std::vector<std::pair<magic::RuneElement, float>> elements;
};

// Create a life essence profile for plant-based food.
// Distribution: EARTH 50%, WATER 35%, LIGHT 15%.
FoodDef make_plant_food(float food_value, float total_essence);

// Create a creature essence profile for animal-based food.
// Distribution: creature_element 70%, EARTH 15%, WATER 10%, LIGHT 5%.
FoodDef make_creature_food(float food_value, float total_essence,
                           magic::RuneElement creature_element);

// ============================================================
// SourceEssencePool — per-element source essence storage
// ============================================================
//
// Stores source essence values per RuneElement in a fixed-size array.
// The total across all elements is clamped to a configurable cap.
// Source essence decays over time and is restored by eating food.

class SourceEssencePool {
public:
    SourceEssencePool() { reset(); }

    // Get essence value for a specific element.
    float get(magic::RuneElement element) const;

    // Add essence to a specific element. Returns actual amount added
    // (may be less if total would exceed cap).
    float add(magic::RuneElement element, float value);

    // Total essence across all elements.
    float total() const;

    // Source essence cap (total across all elements).
    float cap() const { return cap_; }
    void set_cap(float max_val);

    // Decay rate per tick (applied to each element proportionally).
    float decay_rate() const { return decay_rate_; }
    void set_decay_rate(float rate);

    // Decay all elements by decay_rate_ proportionally.
    void decay();

    // Direct set for a specific element (for save/load).
    void set(magic::RuneElement element, float value);

    // Reset all to zero.
    void reset();

    // Raw array access for serialization.
    const std::array<float, static_cast<int>(magic::RuneElement::COUNT)>& values() const {
        return values_;
    }

private:
    static constexpr int kElementCount = static_cast<int>(magic::RuneElement::COUNT);
    std::array<float, kElementCount> values_ = {};
    float cap_ = kDefaultCap;
    float decay_rate_ = kDefaultDecayRate;

    static constexpr float kDefaultCap = 100.0f;
    static constexpr float kDefaultDecayRate = 0.02f;

    void clamp_to_cap();
};

// ============================================================
// SatiationData — player satiation (hunger) system
// ============================================================
//
// Tracks the player's satiation value and computes hunger effects.
// Satiation decays over time; eating food restores it.
// Low satiation causes debuffs (speed, health regen, attack)
// and eventually starvation damage.
//
// Also manages a SourceEssencePool that stores element-tagged
// source essence from food. Source essence acts as an extra
// reserve on top of the player's source reserve (源质储备).

class SatiationData {
public:
    SatiationData() { reset(); }

    // --- Satiation value (0.0 ~ satiation_max) ---
    float satiation_current() const { return satiation_current_; }
    float satiation_max() const { return satiation_max_; }
    float decay_rate() const { return decay_rate_; }

    void set_satiation_max(float max_val);
    void set_decay_rate(float rate);

    // Eat food: restores satiation and element essence.
    // Returns the actual satiation amount restored.
    float eat(const FoodDef& food);

    // Direct set (for save/load or admin commands).
    void set_satiation(float value);

    // --- Hunger level ---
    HungerLevel hunger_level() const;
    bool is_starving() const;

    // --- Effect modifiers (computed from current hunger level) ---
    float speed_modifier() const;
    float health_regen_modifier() const;
    float attack_modifier() const;
    float starvation_damage_per_tick() const;

    // --- Thresholds (configurable) ---
    float peckish_threshold() const { return peckish_threshold_; }
    float hungry_threshold() const { return hungry_threshold_; }
    float starving_threshold() const { return starving_threshold_; }

    void set_peckish_threshold(float val);
    void set_hungry_threshold(float val);
    void set_starving_threshold(float val);

    // --- Source essence pool ---
    SourceEssencePool& source_essence() { return source_essence_; }
    const SourceEssencePool& source_essence() const { return source_essence_; }

    // Convenience: get essence for a specific element.
    float get_source_essence(magic::RuneElement element) const;

    // Convenience: total source essence across all elements.
    float get_total_source_essence() const;

    // Set source essence cap (typically called after computing
    // organ contributions via PlayerSourceLawData).
    void set_source_essence_cap(float max_val);

    float source_essence_cap() const { return source_essence_.cap(); }

    // --- Tick (called each game tick) ---
    // Decays satiation + source essence and returns the new HungerLevel.
    HungerLevel tick();

    // --- Reset to defaults ---
    void reset();

    // --- Serialization ---
    struct SerializedData {
        float satiation_current = kDefaultSatiationMax;
        float satiation_max = kDefaultSatiationMax;
        float decay_rate = kDefaultDecayRate;
        float peckish_threshold = kDefaultPeckishThreshold;
        float hungry_threshold = kDefaultHungryThreshold;
        float starving_threshold = kDefaultStarvingThreshold;
        // Source essence pool
        float source_essence_cap = 100.0f;
        float source_essence_decay_rate = 0.02f;
        std::array<float, static_cast<int>(magic::RuneElement::COUNT)> source_essence_values = {};
    };

    SerializedData to_serialized() const;
    void from_serialized(const SerializedData& data);

private:
    float satiation_current_ = kDefaultSatiationMax;
    float satiation_max_ = kDefaultSatiationMax;
    float decay_rate_ = kDefaultDecayRate;

    float peckish_threshold_ = kDefaultPeckishThreshold;
    float hungry_threshold_ = kDefaultHungryThreshold;
    float starving_threshold_ = kDefaultStarvingThreshold;

    SourceEssencePool source_essence_;

    // Default constants
    static constexpr float kDefaultSatiationMax = 100.0f;
    // 0.05 per tick at 20 TPS = 1.0/sec, ~100 seconds from full to empty
    static constexpr float kDefaultDecayRate = 0.05f;
    static constexpr float kDefaultPeckishThreshold = 30.0f;
    static constexpr float kDefaultHungryThreshold = 15.0f;
    static constexpr float kDefaultStarvingThreshold = 5.0f;

    HungerLevel compute_hunger_level() const;
};

} // namespace science_and_theology
